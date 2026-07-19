import 'dart:convert';

import 'package:flutter/foundation.dart' show debugPrint;
import 'package:riverpod_annotation/riverpod_annotation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../control/application/advanced_session.dart';
import '../../control/application/lamp_save_status.dart';
import '../domain/inventory_lamp.dart';

part 'inventory_notifier.g.dart';

const _prefsKey = 'inventory.v1';

@Riverpod(keepAlive: true, name: 'inventoryNotifierProvider')
class InventoryNotifier extends _$InventoryNotifier {
  @override
  Future<List<InventoryLamp>> build() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_prefsKey);
    if (raw == null || raw.isEmpty) return const [];
    try {
      final list = jsonDecode(raw) as List;
      final out = <InventoryLamp>[];
      for (final e in list) {
        if (e is! Map<String, dynamic>) continue;
        try {
          out.add(InventoryLamp.fromJson(e));
        } catch (err) {
          debugPrint('[inventory] skipping unparseable lamp entry: $err');
        }
      }
      return out;
    } catch (err) {
      // Corrupt blob: recover to empty rather than erroring the whole app.
      // The bytes stay on disk (not cleared) until the first mutation
      // overwrites them, so nothing persists over an inventory that only
      // failed to parse.
      debugPrint('[inventory] inventory JSON corrupt, starting empty: $err');
      return const [];
    }
  }

  Future<void> _persist(List<InventoryLamp> list) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefsKey, jsonEncode(list));
    state = AsyncData(list);
  }

  // Serializes read-modify-write mutations. Each [transform] runs after the
  // previous mutation's persist resolves, against the latest state, so two
  // concurrent callers can't both read the same snapshot and clobber. A
  // transform returning the SAME list reference skips the disk write. Never
  // persists over a non-data state (errored / still loading inventory).
  Future<void> _tail = Future<void>.value();
  Future<void> _mutate(
      List<InventoryLamp> Function(List<InventoryLamp> current) transform) {
    final next = _tail.then((_) async {
      if (!state.hasValue) return;
      final current = state.value ?? const <InventoryLamp>[];
      final updated = transform(current);
      if (identical(updated, current)) return;
      await _persist(updated);
    });
    _tail = next.then((_) {}, onError: (_) {});
    return next;
  }

  Future<void> add(InventoryLamp lamp) => _mutate((current) {
        if (current.any((l) => l.id == lamp.id)) return current;
        return [...current, lamp];
      });

  Future<void> remove(String id) async {
    await _mutate((current) {
      if (!current.any((l) => l.id == id)) return current;
      return current.where((l) => l.id != id).toList();
    });
    _invalidatePerLampState(id);
  }

  /// Invalidate every `keepAlive: true` per-lamp family provider so the
  /// removed lamp doesn't leave orphaned state pinned forever. The
  /// non-keepAlive families (controlNotifier, wispNotifier,
  /// firmwareNotifier) auto-dispose when their last consumer unmounts,
  /// so no explicit invalidation is required for them.
  ///
  /// `expressionDraftProvider` is keyed by `(lampId, type, target)`.
  /// Invalidating it requires knowing every (type, target) tuple that
  /// might have been instantiated for this lamp. Skipped here since each
  /// `ExpressionConfig` draft is tiny (~hundred
  /// bytes); the next call to `expressionDraftProvider(lampId, ...)`
  /// for an unknown lamp falls through to the default-config branch,
  /// so stale drafts can't be re-read meaningfully. Re-adopting the
  /// same lamp id resurrects old drafts (acceptable corner case).
  ///
  /// If a new keepAlive family keyed by lamp id ALONE is ever added,
  /// add it here. The list lives in one place so the contract is
  /// reviewable.
  void _invalidatePerLampState(String id) {
    ref.invalidate(lampSaveStatusProvider(id));
    ref.invalidate(advancedSessionProvider(id));
  }

  Future<void> updateSeen(
    String id, {
    List<int>? shade,
    List<int>? base,
  }) =>
      _mutate((current) {
        final now = DateTime.now().millisecondsSinceEpoch;
        return current.map((l) {
          if (l.id != id) return l;
          return l.copyWith(
            lastSeenEpochMs: now,
            lastShadeColor: shade ?? l.lastShadeColor,
            lastBaseColor: base ?? l.lastBaseColor,
          );
        }).toList();
      });

  /// Bump only `lastSeenEpochMs` for the given id. Used by the BLE-adv
  /// stream to keep My Lamps' "last seen" subtitle accurate without
  /// having to round-trip a GATT connect first. Returns silently when
  /// the id isn't paired or when the timestamp is fresher than 30 s
  /// (so a 44 Hz adv stream doesn't burn SharedPreferences I/O; see
  /// nearby_lamps_notifier debounce too, but this is the belt-and-
  /// suspenders cap so the inventory provider can't be hammered by a
  /// future caller that forgets the gate).
  Future<void> touchLastSeen(String id) => _mutate((current) {
        final now = DateTime.now().millisecondsSinceEpoch;
        final idx = current.indexWhere((l) => l.id == id);
        if (idx < 0) return current;
        final prior = current[idx].lastSeenEpochMs ?? 0;
        if (now - prior < 30000) return current;
        final updated = [...current];
        updated[idx] = current[idx].copyWith(lastSeenEpochMs: now);
        return updated;
      });

  /// Remember the latest observed `isMesh` for this lamp. Called by
  /// `nearby_lamps_notifier` whenever an adv arrives. Used by
  /// `lampRouteResolver` to route correctly when the lamp is out of
  /// range. Without this, a legacy BT-only lamp that's currently
  /// silent would route to ControlScreen and strand the user on
  /// ConnectingView.
  ///
  /// No-op when the value matches the cached one (the common case once
  /// the cache is warm) to avoid writing to SharedPreferences on every
  /// adv. The first adv flips a `null` cache to the real value; flips
  /// after a firmware upgrade flip the cached bool.
  Future<void> rememberMeshState(String id, {required bool isMesh}) =>
      _mutate((current) {
        final idx = current.indexWhere((l) => l.id == id);
        if (idx < 0) return current;
        if (current[idx].lastKnownIsMesh == isMesh) return current;
        final updated = [...current];
        updated[idx] = current[idx].copyWith(lastKnownIsMesh: isMesh);
        return updated;
      });

  /// Reconcile a new BLE `id` with an inventory entry that has a
  /// matching identity (name + base + shade). Returns the matched
  /// entry's previous id when a reconciliation happened, null otherwise.
  ///
  /// The BLE `id` exposed via flutter_blue_plus is platform-derived:
  /// the Bluetooth MAC on Android, but a CoreBluetooth UUID on iOS that
  /// can change across reinstalls / iCloud restores. When the user
  /// re-installs the iOS app (or migrates Android → iOS while syncing
  /// inventory via SharedPreferences), the lamp shows up under a new
  /// `id`, the old inventory entry orphans, and the lamp re-appears
  /// in the Add Lamp scan as if it were unknown.
  ///
  /// This method handles that: if exactly ONE inventory entry has the
  /// same name + baseRgb + shadeRgb as the incoming adv, update that
  /// entry's id to the new value. Multiple matches → no-op (ambiguous:
  /// two lamps with the same identity collide; safer not to merge).
  /// Zero matches → no-op (genuinely new lamp). Already-matching id →
  /// no-op (the fast-path; happens on every adv after the first match).
  Future<String?> reconcileIdByIdentity({
    required String newId,
    required String name,
    required int baseRgb,
    required int shadeRgb,
  }) {
    // Serialized on the same tail as [_mutate] so a concurrent adv-driven
    // mutation can't reorder against the id rewrite.
    final next = _tail.then<String?>((_) async {
      if (!state.hasValue) return null;
      final current = state.value ?? const <InventoryLamp>[];
      if (current.any((l) => l.id == newId)) return null;
      final candidates = current
          .where((l) =>
              l.name == name &&
              l.lastBaseColor != null &&
              l.lastShadeColor != null &&
              _matchesPackedRgb(l.lastBaseColor!, baseRgb) &&
              _matchesPackedRgb(l.lastShadeColor!, shadeRgb))
          .toList();
      if (candidates.length != 1) return null;
      final priorId = candidates.first.id;
      final updated = current.map((l) {
        if (l.id != priorId) return l;
        return l.copyWith(id: newId);
      }).toList();
      await _persist(updated);
      return priorId;
    });
    _tail = next.then((_) {}, onError: (_) {});
    return next;
  }

  // Compares an `InventoryLamp.lastShadeColor / lastBaseColor`
  // (`[R, G, B, W?]` list) against an adv-derived packed
  // 0x00RRGGBB int. The W byte is ignored for identity matching;
  // BLE adv only carries R/G/B.
  static bool _matchesPackedRgb(List<int> rgbw, int packed) {
    if (rgbw.length < 3) return false;
    final r = (packed >> 16) & 0xFF;
    final g = (packed >> 8) & 0xFF;
    final b = packed & 0xFF;
    return rgbw[0] == r && rgbw[1] == g && rgbw[2] == b;
  }

  /// Set the lamp's variant identity (`'standard'`, `'snafu'`, ...) from
  /// the post-connect CHAR_LAMP_SECTION read. Persisted so OTA can fetch
  /// the matching per-variant firmware even when the lamp is offline.
  Future<void> updateLampType(String id, String lampType) =>
      _mutate((current) {
        final idx = current.indexWhere((l) => l.id == id);
        if (idx < 0) return current;
        if (current[idx].lampType == lampType) return current;
        final updated = [...current];
        updated[idx] = current[idx].copyWith(lampType: lampType);
        return updated;
      });

  /// Mirror the lamp's raw mesh MAC from the post-connect
  /// CHAR_LAMP_SECTION read, so nearby-peer cross-reference matches on iOS
  /// (where `id` is a CoreBluetooth UUID, not the address peers observe).
  Future<void> updateLampId(String id, String lampId) => _mutate((current) {
        final idx = current.indexWhere((l) => l.id == id);
        if (idx < 0) return current;
        if (current[idx].lampId == lampId) return current;
        final updated = [...current];
        updated[idx] = current[idx].copyWith(lampId: lampId);
        return updated;
      });

  /// Mirror the lamp's reported firmware version + channel onto inventory
  /// so My Lamps can render `v1.0.82 · stable` per tile even when the
  /// lamp is offline. Both args nullable: lamps that don't emit
  /// fwVersion/fwChannel keep their last-known values.
  Future<void> updateFirmwareInfo(
    String id, {
    int? fwVersion,
    String? fwChannel,
  }) =>
      _mutate((current) {
        final idx = current.indexWhere((l) => l.id == id);
        if (idx < 0) return current;
        final cur = current[idx];
        final newVersion = fwVersion ?? cur.fwVersion;
        final newChannel = fwChannel ?? cur.fwChannel;
        if (newVersion == cur.fwVersion && newChannel == cur.fwChannel) {
          return current;
        }
        final updated = [...current];
        updated[idx] =
            cur.copyWith(fwVersion: newVersion, fwChannel: newChannel);
        return updated;
      });

  /// Set the auth password the app uses to authenticate with this lamp.
  /// Called by ControlNotifier.setLampPassword right before pushing the
  /// new password to the firmware, so the post-reboot reconnect uses the
  /// new value. Passing null clears it (matches the factory-fresh state).
  Future<void> updatePassword(String id, String? password) =>
      _mutate((current) => current.map((l) {
            if (l.id != id) return l;
            return l.copyWith(controlPassword: password);
          }).toList());

  /// Update the cached display name for a lamp. Called by ControlNotifier
  /// after a successful settings_blob save (and on cold-load) so the
  /// `MyLampsScreen` rows reflect the lamp's current name. Otherwise the
  /// picker keeps showing the name captured at adopt-time even though the
  /// control screen's "Hello my name is:" header (which reads live state)
  /// shows the updated one. No-op when the id isn't in the inventory.
  Future<void> updateName(String id, String name) =>
      _mutate((current) => current.map((l) {
            if (l.id != id) return l;
            return l.copyWith(name: name);
          }).toList());
}
