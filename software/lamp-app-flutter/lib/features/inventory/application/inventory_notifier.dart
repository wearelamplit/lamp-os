import 'dart:convert';

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
    final decoded = (jsonDecode(raw) as List)
        .cast<Map<String, dynamic>>()
        .map(InventoryLamp.fromJson)
        .toList();
    return decoded;
  }

  Future<void> _persist(List<InventoryLamp> list) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefsKey, jsonEncode(list));
    state = AsyncData(list);
  }

  Future<void> add(InventoryLamp lamp) async {
    final current = state.value ?? const [];
    if (current.any((l) => l.id == lamp.id)) return;
    await _persist([...current, lamp]);
  }

  Future<void> remove(String id) async {
    final current = state.value ?? const [];
    await _persist(current.where((l) => l.id != id).toList());
    _invalidatePerLampState(id);
  }

  /// Invalidate every `keepAlive: true` per-lamp family provider so the
  /// removed lamp doesn't leave orphaned state pinned forever (audit
  /// cq-C2). The non-keepAlive families (controlNotifier, wispNotifier,
  /// firmwareNotifier) auto-dispose when their last consumer unmounts,
  /// so no explicit invalidation is required for them.
  ///
  /// `expressionDraftProvider` is keyed by `(lampId, type, target)` —
  /// invalidating it requires knowing every (type, target) tuple that
  /// might have been instantiated for this lamp. We skip it here on
  /// the basis that each `ExpressionConfig` draft is tiny (~hundred
  /// bytes); the next call to `expressionDraftProvider(lampId, ...)`
  /// for an unknown lamp falls through to the default-config branch,
  /// so stale drafts can't be re-read meaningfully. Re-adopting the
  /// same lamp id would resurrect old drafts — acceptable corner case.
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
  }) async {
    final current = state.value ?? const [];
    final now = DateTime.now().millisecondsSinceEpoch;
    final updated = current.map((l) {
      if (l.id != id) return l;
      return l.copyWith(
        lastSeenEpochMs: now,
        lastShadeColor: shade ?? l.lastShadeColor,
        lastBaseColor: base ?? l.lastBaseColor,
      );
    }).toList();
    await _persist(updated);
  }

  /// Bump only `lastSeenEpochMs` for the given id. Used by the BLE-adv
  /// stream to keep My Lamps' "last seen" subtitle accurate without
  /// having to round-trip a GATT connect first. Returns silently when
  /// the id isn't paired or when the timestamp is fresher than 30 s
  /// (so a 44 Hz adv stream doesn't burn SharedPreferences I/O — see
  /// nearby_lamps_notifier debounce too, but this is the belt-and-
  /// suspenders cap so the inventory provider can't be hammered by a
  /// future caller that forgets the gate).
  Future<void> touchLastSeen(String id) async {
    final current = state.value ?? const [];
    final now = DateTime.now().millisecondsSinceEpoch;
    final idx = current.indexWhere((l) => l.id == id);
    if (idx < 0) return;
    final prior = current[idx].lastSeenEpochMs ?? 0;
    if (now - prior < 30000) return;
    final updated = [...current];
    updated[idx] = current[idx].copyWith(lastSeenEpochMs: now);
    await _persist(updated);
  }

  /// Remember the latest observed `isMesh` for this lamp. Called by
  /// `nearby_lamps_notifier` whenever an adv arrives. Used by
  /// `lampRouteResolver` to route correctly when the lamp is out of
  /// range — without this, a legacy BT-only lamp that's currently
  /// silent would route to ControlScreen and strand the user on
  /// ConnectingView.
  ///
  /// No-op when the value matches the cached one (the common case once
  /// the cache is warm) so we don't write to SharedPreferences on every
  /// adv. The first adv flips a `null` cache to the real value; flips
  /// after a firmware upgrade flip the cached bool.
  Future<void> rememberMeshState(String id, {required bool isMesh}) async {
    final current = state.value ?? const [];
    final idx = current.indexWhere((l) => l.id == id);
    if (idx < 0) return;
    if (current[idx].lastKnownIsMesh == isMesh) return;
    final updated = [...current];
    updated[idx] = current[idx].copyWith(lastKnownIsMesh: isMesh);
    await _persist(updated);
  }

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
  /// entry's id to the new value. Multiple matches → no-op (ambiguous
  /// — two lamps with the same identity collide; safer not to merge).
  /// Zero matches → no-op (genuinely new lamp). Already-matching id →
  /// no-op (the fast-path; happens on every adv after the first match).
  Future<String?> reconcileIdByIdentity({
    required String newId,
    required String name,
    required int baseRgb,
    required int shadeRgb,
  }) async {
    final current = state.value ?? const [];
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
  Future<void> updateLampType(String id, String lampType) async {
    final current = state.value ?? const [];
    final idx = current.indexWhere((l) => l.id == id);
    if (idx < 0) return;
    if (current[idx].lampType == lampType) return;
    final updated = [...current];
    updated[idx] = current[idx].copyWith(lampType: lampType);
    state = AsyncData(updated);
    await _persist(updated);
  }

  /// Mirror the lamp's persistent dev-mode flag onto inventory so
  /// `effectiveAdvancedProvider` can answer without a connection.
  /// Called from ControlNotifier after each successful section read +
  /// after the dev-mode-toggle write completes.
  Future<void> updateDevMode(String id, bool devMode) async {
    final current = state.value ?? const [];
    final idx = current.indexWhere((l) => l.id == id);
    if (idx < 0) return;
    if (current[idx].devMode == devMode) return;
    final updated = [...current];
    updated[idx] = current[idx].copyWith(devMode: devMode);
    state = AsyncData(updated);
    await _persist(updated);
  }

  /// Mirror the lamp's reported firmware version + channel onto inventory
  /// so My Lamps can render `v1.0.82 · stable` per tile even when the
  /// lamp is offline. Both args nullable — pre-Phase-H lamps that don't
  /// emit fwVersion/fwChannel just keep their last-known values.
  Future<void> updateFirmwareInfo(
    String id, {
    int? fwVersion,
    String? fwChannel,
  }) async {
    final current = state.value ?? const [];
    final idx = current.indexWhere((l) => l.id == id);
    if (idx < 0) return;
    final cur = current[idx];
    final newVersion = fwVersion ?? cur.fwVersion;
    final newChannel = fwChannel ?? cur.fwChannel;
    if (newVersion == cur.fwVersion && newChannel == cur.fwChannel) return;
    final updated = [...current];
    updated[idx] = cur.copyWith(fwVersion: newVersion, fwChannel: newChannel);
    state = AsyncData(updated);
    await _persist(updated);
  }

  /// Set the auth password the app uses to authenticate with this lamp.
  /// Called by ControlNotifier.setLampPassword right before pushing the
  /// new password to the firmware, so the post-reboot reconnect uses the
  /// new value. Passing null clears it (matches the factory-fresh state).
  Future<void> updatePassword(String id, String? password) async {
    final current = state.value ?? const [];
    final updated = current.map((l) {
      if (l.id != id) return l;
      return l.copyWith(controlPassword: password);
    }).toList();
    await _persist(updated);
  }

  /// Update the cached display name for a lamp. Called by ControlNotifier
  /// after a successful settings_blob save (and on cold-load) so the
  /// `MyLampsScreen` rows reflect the lamp's current name — otherwise the
  /// picker keeps showing the name captured at adopt-time even though the
  /// control screen's "Hello my name is:" header (which reads live state)
  /// shows the updated one. No-op when the id isn't in the inventory.
  Future<void> updateName(String id, String name) async {
    final current = state.value ?? const [];
    final updated = current.map((l) {
      if (l.id != id) return l;
      return l.copyWith(name: name);
    }).toList();
    await _persist(updated);
  }
}
