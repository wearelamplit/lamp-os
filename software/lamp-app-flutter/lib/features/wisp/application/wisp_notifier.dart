import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show debugPrint;
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../control/domain/lamp_color.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/uuids.dart';
import '../data/wisp_repository.dart';
import '../domain/zone_source.dart';
import '../domain/wisp_source_mode.dart';
import '../domain/wisp_status.dart';

part 'wisp_notifier.g.dart';

/// Owns the live [WispStatus] for a single lamp. On build it does one
/// read of `CHAR_WISP_STATUS` and subscribes to its notify stream;
/// thereafter every wispStatus update from the wisp lands in `state`
/// without a round-trip.
///
/// `setZone` / `clearZone` delegate to the repository and rely on the
/// wisp's on-change broadcast (≤ ~2s) to push the updated status back
/// via CHAR_WISP_STATUS. We optimistically reflect the choice in local
/// state so the chip highlight doesn't lag the tap; the notify either
/// confirms or corrects it.
@Riverpod(name: 'wispNotifierProvider')
class WispNotifier extends _$WispNotifier {
  StreamSubscription<Uint8List>? _sub;
  late WispRepository _repo;

  /// Debounce window for the realtime wispOp writes that fire on every
  /// Off-mode color-picker drag tick or manual-palette editor mutation.
  /// 250 ms gives a continuous picker drag ~4 writes/sec — fast enough
  /// to feel realtime, slow enough that NVS wear and BLE bandwidth stay
  /// reasonable.
  static const Duration _offColorDebounce =
      Duration(milliseconds: 250);
  static const Duration _manualPaletteDebounce =
      Duration(milliseconds: 250);
  Timer? _offColorWriteTimer;
  Timer? _manualPaletteWriteTimer;

  /// Optimistic-write guard for [setSource]. The wisp's response chain
  /// is: app BLE-writes wispOp → relay lamp forwards via MSG_CONTROL_OP
  /// → wisp dispatches setSource → triggerOnChange emits new wispStatus
  /// → wispStatus relays back to lamps → BLE notify fires. During that
  /// round-trip (up to seconds, especially via multi-hop relay), the
  /// relay lamp keeps notifying its CACHED wispStatus — still carrying
  /// the previous `source` — which races our optimistic state and flips
  /// the picker back. While `_pendingSourceMode` is set, incoming
  /// wispStatus notifications have their `source` field replaced with
  /// the pending value until either (a) a wispStatus arrives whose
  /// source matches the pending mode (write confirmed), or (b) the
  /// 3 s window expires (write missed — let the real state through so
  /// the picker snaps back and the user can retry).
  static const Duration _sourceWriteGuard = Duration(seconds: 3);
  WispSourceMode? _pendingSourceMode;
  DateTime? _pendingSourceUntil;

  // -- Manual palette state ------------------------------------------------
  // The wisp's read leg (CHAR_WISP_STATUS READ) is the source of truth
  // for the saved palette. On build the notifier reads the full status
  // which includes the base64-packed palette under the `palette` key;
  // that value seeds the editor. NOTIFY payloads are trimmed (no palette
  // blob), so a changed `paletteIdPrefix` in a NOTIFY triggers a fresh
  // READ to pull the updated palette.
  //
  // Two slots: `_savedManualPalette` is the last value the user committed
  // via [setManualPalette]; `_draftManualPalette` is the in-flight editor
  // state. The UI gates the save button on `_draftManualPalette !=
  // _savedManualPalette`.
  List<LampColor> _savedManualPalette = const <LampColor>[];
  List<LampColor> _draftManualPalette = const <LampColor>[];

  bool _currentPaletteKnown = false;
  String _lastPaletteIdPrefix = '';
  bool _paletteRereadInFlight = false;

  Set<String>? _claimedMacs;
  bool _claimsReadInFlight = false;

  /// The set of lamp mesh MACs the wisp currently claims, or null while
  /// the first CHAR_WISP_CLAIMS read is in flight. Empty set means the
  /// wisp reported count=0 (no claims / stale).
  Set<String>? get claimedMacs => _claimedMacs;

  /// The currently-saved manual palette (last committed). Empty before
  /// the first save in a session.
  List<LampColor> get savedManualPalette => _savedManualPalette;

  /// The in-flight editor draft. Mutates as the user adds, edits, reorders
  /// or deletes swatches; flushed to the wisp by [setManualPalette].
  List<LampColor> get draftManualPalette => _draftManualPalette;

  /// True when the editor has unsaved changes. Drives the save button's
  /// enabled state in the UI.
  bool get manualPaletteDirty {
    if (_draftManualPalette.length != _savedManualPalette.length) return true;
    for (var i = 0; i < _draftManualPalette.length; i++) {
      if (_draftManualPalette[i] != _savedManualPalette[i]) return true;
    }
    return false;
  }

  /// True while the wisp is present but its palette hasn't been received
  /// from the read leg yet. Drives a loading placeholder in the editor.
  bool get paletteLoading =>
      (state.value?.present ?? false) && !_currentPaletteKnown;

  bool _disposed = false;

  @override
  Future<WispStatus> build(String lampId) async {
    final ble = ref.read(bleClientProvider);
    _repo = WispRepository(ble, lampId);

    _currentPaletteKnown = false;
    _lastPaletteIdPrefix = '';
    _paletteRereadInFlight = false;

    _claimedMacs = null;
    _claimsReadInFlight = false;

    ref.onDispose(() {
      _disposed = true;
      _sub?.cancel();
      _offColorWriteTimer?.cancel();
      _manualPaletteWriteTimer?.cancel();
    });

    debugPrint('[wisp_notifier] build lamp=$lampId -- waiting for connect');

    // Wait until the BLE link is connected before subscribing or reading.
    // `watchConnected` emits the current state immediately on listen, so
    // this is a no-op when we're already connected. If the lamp_shell
    // mounted us mid-connect, we await the first `true` emission rather
    // than racing the GATT layer and failing with BleNotFound.
    await ble.watchConnected(lampId).firstWhere((connected) => connected);

    // If the notifier was disposed during the await (user navigated to a
    // different lamp before connect resolved), bail. Mutating `state` or
    // touching `ref` after dispose throws.
    if (_disposed) {
      debugPrint(
        '[wisp_notifier] build lamp=$lampId -- disposed during connect await, bailing',
      );
      return WispStatus.empty;
    }

    debugPrint(
      '[wisp_notifier] build lamp=$lampId -- connected, subscribing + reading',
    );

    // Subscribe before the initial read so we never miss a notify that
    // fires between the read returning and the listener attaching.
    _sub = ble
        .subscribe(lampId, BleUuids.controlService, BleUuids.wispStatus)
        .listen((bytes) {
      if (_disposed) return;
      var next = WispStatus.fromBytes(bytes);
      // A frame that decodes to "no wisp" while we already hold a present
      // status is a transient empty/short BLE notify, not the wisp actually
      // vanishing. Dropping it keeps the icon, source, and palette from
      // blinking out for a tick whenever a frame lands empty. A genuine
      // wisp-gone is re-established by the next build/read of the status.
      if (!next.present && (state.value?.present ?? false)) {
        return;
      }
      final rawMac = next.wispMac;
      next = _applySourceWriteGuard(next);
      _ingestManualPaletteFromStatus(next.currentPalette);
      _maybeRereadForPalette(next);
      unawaited(_loadClaims());
      state = AsyncData(next);
      debugPrint(
        '[wisp_notifier] notify lamp=$lampId len=${bytes.length} '
        'wispMac=$rawMac present=${next.present} source=${next.source}',
      );
    }, onError: (Object e, StackTrace st) {
      debugPrint('[wisp_notifier] notify-stream error lamp=$lampId: $e');
    }, onDone: () {
      debugPrint('[wisp_notifier] notify-stream done lamp=$lampId');
    });

    try {
      // The lamp gates the CHAR_WISP_STATUS read on the AES-GCM auth
      // handshake, which completes a beat after the `connected` edge; a
      // pre-auth read comes back empty (present == false). Retry across the
      // auth window so a status only readable post-auth isn't lost: the
      // wispStatus notify is edge-triggered (fires when the wisp starts or
      // stops controlling a surface) and won't fire to correct it while the
      // wisp paints steadily.
      var initial = _applySourceWriteGuard(await _repo.readStatus());
      if (_disposed) return WispStatus.empty;
      for (var attempt = 0; !initial.present && attempt < 8; attempt++) {
        await Future.delayed(const Duration(milliseconds: 1500));
        if (_disposed) return WispStatus.empty;
        initial = _applySourceWriteGuard(await _repo.readStatus());
        if (_disposed) return WispStatus.empty;
      }
      _ingestManualPaletteFromStatus(initial.currentPalette);
      _lastPaletteIdPrefix = initial.paletteIdPrefix;
      unawaited(_loadClaims());
      debugPrint(
        '[wisp_notifier] initial-read lamp=$lampId '
        'wispMac=${initial.wispMac} present=${initial.present} '
        'source=${initial.source}',
      );
      return initial;
    } catch (e) {
      // After connected==true, GATT can still fail if the link drops
      // between the connected-edge and our read. Return empty so the UI
      // doesn't hang; the notify subscription will catch it up when the
      // next status broadcast lands.
      debugPrint(
        '[wisp_notifier] initial-read FAILED lamp=$lampId -- '
        'returning empty. err=$e',
      );
      return WispStatus.empty;
    }
  }

  /// Consume the currentPalette field from a fresh wispStatus payload.
  /// The read leg serves the full palette blob; this seeds the editor
  /// and marks the palette as known.
  ///
  /// Doesn't touch the draft if the user is mid-edit (draft has diverged
  /// from the previous saved). The next save replaces the wisp's view
  /// anyway.
  void _ingestManualPaletteFromStatus(List<LampColor>? palette) {
    if (palette == null || palette.isEmpty) return;
    _currentPaletteKnown = true;
    if (_palettesEqual(_savedManualPalette, palette)) return;
    // Capture pristine-vs-OLD-saved BEFORE we overwrite -- the pristine
    // check is "did the user touch the draft since the last save".
    final wasDraftPristine = _draftManualPalette.isEmpty ||
        _palettesEqual(_draftManualPalette, _savedManualPalette);
    _savedManualPalette = List<LampColor>.unmodifiable(palette);
    if (wasDraftPristine) {
      _draftManualPalette = List<LampColor>.from(_savedManualPalette);
    }
  }

  static bool _palettesEqual(List<LampColor> a, List<LampColor> b) {
    if (a.length != b.length) return false;
    for (var i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }

  // Serialized against the palette re-read: FbpBleClient has no internal lock,
  // so two concurrent characteristic reads stall the GATT flow on Android.
  Future<void> _loadClaims() async {
    if (_claimsReadInFlight || _paletteRereadInFlight || _disposed) return;
    _claimsReadInFlight = true;
    try {
      final macs = await _repo.readClaims();
      if (_disposed) return;
      _claimedMacs = macs;
      _bumpState();
    } finally {
      _claimsReadInFlight = false;
    }
  }

  // NOTIFY is trimmed (no palette); a changed paletteIdPrefix means the
  // wisp's palette moved, so pull the full status from the READ leg.
  void _maybeRereadForPalette(WispStatus next) {
    final prefix = next.paletteIdPrefix;
    if (prefix.isEmpty || prefix == _lastPaletteIdPrefix) return;
    // Don't consume the prefix if a read is busy; retry on the next notify.
    if (_paletteRereadInFlight || _claimsReadInFlight) return;
    _lastPaletteIdPrefix = prefix;
    _paletteRereadInFlight = true;
    unawaited(() async {
      try {
        final full = _applySourceWriteGuard(await _repo.readStatus());
        if (_disposed) return;
        _ingestManualPaletteFromStatus(full.currentPalette);
        _lastPaletteIdPrefix = full.paletteIdPrefix;
        _bumpState();
      } catch (e, st) {
        debugPrint('WispNotifier palette re-read failed: $e\n$st');
      } finally {
        _paletteRereadInFlight = false;
      }
    }());
  }

  /// Pin the wisp to [zoneId]. Optimistically updates local state to
  /// `zoneSource: appOp` so the UI feels responsive; the next status
  /// notify will replace this with the wisp's authoritative view.
  ///
  /// On BLE write failure, rolls the optimistic state back to its
  /// pre-call snapshot before rethrowing (audit perf-M8). Without the
  /// rollback the user sees the chip "stick" on the failed selection
  /// even though the wisp never received the op -- and there's no notify
  /// coming to reconcile it.
  Future<void> setZone(int zoneId) async {
    final prev = state;
    final cur = state.value ?? WispStatus.empty;
    state = AsyncData(cur.copyWith(
      currentZone: zoneId,
      zoneSource: ZoneSource.appOp,
    ));
    try {
      await _repo.setZone(zoneId);
    } catch (e, st) {
      debugPrint('WispNotifier.setZone($zoneId) failed: $e\n$st');
      state = prev;
      rethrow;
    }
  }

  /// Clear the persisted zone pin. After the wisp processes this, the
  /// next status update will show `zoneSource: firstSeen` (or `none`
  /// if no zone has been observed yet).
  ///
  /// Rolls optimistic state back on write failure (see [setZone]).
  Future<void> clearZone() async {
    final prev = state;
    final cur = state.value ?? WispStatus.empty;
    state = AsyncData(cur.copyWith(
      // Don't optimistically null currentZone -- the wisp may keep
      // following whatever it was on until firstSeen kicks in. Just
      // flip the source so the "Clear selection" button hides.
      zoneSource: ZoneSource.firstSeen,
    ));
    try {
      await _repo.clearZone();
    } catch (e, st) {
      debugPrint('WispNotifier.clearZone() failed: $e\n$st');
      state = prev;
      rethrow;
    }
  }

  /// Push WiFi credentials to the wisp. The wisp persists into NVS, calls
  /// `WifiLink::reconnect()` to associate with the new AP, and refreshes
  /// the BLE manufacturer-data advert so pre-mesh lamps see the new SSID
  /// on their next scan. The connection state surfaces back through
  /// `WispStatus.wifiConnected` on the next status notify (typically
  /// within ~2s of the wisp's STA association completing).
  ///
  /// We don't optimistically flip `wifiConnected` to true -- the wisp
  /// may fail to associate (wrong password, AP out of range) and a
  /// "Connected" badge that doesn't budge would mislead the operator.
  /// The UI surfaces the "save sent" state via its own local flag.
  Future<void> setWifi(String ssid, String password) async {
    try {
      await _repo.setWifi(ssid, password);
    } catch (e, st) {
      debugPrint('WispNotifier.setWifi() failed: $e\n$st');
      rethrow;
    }
  }

  /// Pick the color the wisp renders on its OWN 30-pixel ring while
  /// sourceMode is Off. Does not broadcast to lamps -- Off mode keeps
  /// PaintDistributor held off. Optimistically reflects in local state
  /// so the swatch updates instantly; the BLE write itself is debounced
  /// at [_offColorDebounce] so a continuous picker drag tops out at
  /// ~4 writes/sec instead of one per drag tick (which would saturate
  /// the wispOp link and chew through NVS writes on the wisp's side).
  ///
  /// No rollback on failure -- the user expects realtime, not
  /// reconcile-and-revert. The wisp's next status notify will correct
  /// local state if the write actually missed.
  Future<void> setOffColor(LampColor color) async {
    final cur = state.value ?? WispStatus.empty;
    state = AsyncData(cur.copyWith(offColor: color));
    _offColorWriteTimer?.cancel();
    _offColorWriteTimer = Timer(_offColorDebounce, () async {
      try {
        await _repo.setOffColor(color);
      } catch (e, st) {
        debugPrint('WispNotifier.setOffColor write failed: $e\n$st');
      }
    });
  }

  /// Shuffle: tell the wisp to bump its seed and re-roll per-lamp color
  /// assignments. The updated seed rides back in the next wispStatus so
  /// the app preview re-rolls in lock-step without optimistic state here.
  Future<void> shuffle() async {
    try {
      await _repo.shuffle();
    } catch (e, st) {
      debugPrint('WispNotifier.shuffle() failed: $e\n$st');
      rethrow;
    }
  }

  /// Set the wisp source mode (Off / Manual / Aurora).
  /// Optimistically reflects in local state so the pill picker doesn't
  /// lag the tap; the wispStatus notify reconciles within ~2s. Rolls
  /// back the optimistic state on BLE write failure (audit perf-M8).
  ///
  /// Arms the source-write guard so stale wispStatus echoes from the
  /// relay lamp between the BLE write and the wisp's triggerOnChange
  /// response don't flip the picker back. See [_sourceWriteGuard].
  Future<void> setSource(WispSourceMode mode) async {
    final prev = state;
    final cur = state.value ?? WispStatus.empty;
    debugPrint(
      '[wisp_notifier] setSource lamp=$lampId mode=$mode '
      'cur.wispMac=${cur.wispMac} cur.present=${cur.present} '
      'fellBackToEmpty=${state.value == null}',
    );
    state = AsyncData(cur.copyWith(source: mode));
    _pendingSourceMode = mode;
    _pendingSourceUntil = DateTime.now().add(_sourceWriteGuard);
    try {
      await _repo.setSource(mode);
    } catch (e, st) {
      debugPrint('WispNotifier.setSource($mode) failed: $e\n$st');
      state = prev;
      // On failure, clear the guard rather than restoring its prior
      // value. The optimistic state is also being rolled back, so
      // suppression of "stale" notifies should stop -- the user needs
      // to see ground truth (whatever the wisp is actually reporting).
      _pendingSourceMode = null;
      _pendingSourceUntil = null;
      rethrow;
    }
  }

  /// Apply the [setSource] optimistic-write guard to an incoming
  /// [WispStatus]. When the guard is armed and the incoming `source`
  /// still carries the stale pre-write value, the field is replaced
  /// with the pending mode (everything else flows through unchanged).
  /// The guard releases when the wisp confirms (incoming.source ==
  /// pending) or the window expires.
  WispStatus _applySourceWriteGuard(WispStatus incoming) {
    final pendingMode = _pendingSourceMode;
    if (pendingMode == null) return incoming;
    final now = DateTime.now();
    final until = _pendingSourceUntil;
    if (until == null || now.isAfter(until)) {
      _pendingSourceMode = null;
      _pendingSourceUntil = null;
      return incoming;
    }
    if (incoming.source == pendingMode) {
      _pendingSourceMode = null;
      _pendingSourceUntil = null;
      return incoming;
    }
    return incoming.copyWith(source: pendingMode);
  }

  // -- Manual palette editor state-mutators --------------------------------
  // Each helper rebuilds the draft list and rebroadcasts state so any
  // ConsumerWidget watching the notifier rebuilds. We rebuild rather
  // than mutate in place so equality-based diffs (e.g. flutter_riverpod's
  // selectors) actually fire.

  /// Seed the draft from the saved snapshot. Called by the UI on first
  /// open of the editor so the swatches reflect what was last committed.
  void resetManualPaletteDraft() {
    _draftManualPalette = List<LampColor>.from(_savedManualPalette);
    _bumpState();
  }

  /// Append a swatch to the draft. Caps at 10 -- anything beyond is
  /// silently ignored so the UI's `+` button can be permissive.
  void appendManualPaletteColor(LampColor color) {
    if (_draftManualPalette.length >= 10) return;
    _draftManualPalette = [..._draftManualPalette, color];
    _bumpState();
    _scheduleManualPaletteWrite();
  }

  /// Replace the swatch at [index]. No-op if [index] is out of range.
  void updateManualPaletteColor(int index, LampColor color) {
    if (index < 0 || index >= _draftManualPalette.length) return;
    final next = List<LampColor>.from(_draftManualPalette);
    next[index] = color;
    _draftManualPalette = next;
    _bumpState();
    _scheduleManualPaletteWrite();
  }

  /// Remove the swatch at [index] (swipe-to-delete).
  void removeManualPaletteColor(int index) {
    if (index < 0 || index >= _draftManualPalette.length) return;
    final next = List<LampColor>.from(_draftManualPalette);
    next.removeAt(index);
    _draftManualPalette = next;
    _bumpState();
    _scheduleManualPaletteWrite();
  }

  /// Drag-to-reorder. Standard "move item at [oldIndex] to [newIndex]"
  /// semantics -- newIndex is the index in the list AFTER removal.
  void reorderManualPaletteColor(int oldIndex, int newIndex) {
    if (oldIndex < 0 || oldIndex >= _draftManualPalette.length) return;
    final next = List<LampColor>.from(_draftManualPalette);
    final item = next.removeAt(oldIndex);
    final clampedNew = newIndex.clamp(0, next.length);
    next.insert(clampedNew, item);
    _draftManualPalette = next;
    _bumpState();
    _scheduleManualPaletteWrite();
  }

  /// Trailing-edge debounced write of the current draft palette to the
  /// wisp. Called from every editor mutation. Operator-facing semantics:
  /// every edit is "saved" -- no Save button to remember. On the wire,
  /// continuous edits collapse into one write per [_manualPaletteDebounce]
  /// window so NVS wear stays bounded.
  ///
  /// Errors are logged and swallowed (no UI-visible failure path); the
  /// next wispStatus notify will let us know if the wisp is still
  /// receiving. Realtime UX trumps reconcile-and-revert here.
  void _scheduleManualPaletteWrite() {
    _manualPaletteWriteTimer?.cancel();
    final committed = List<LampColor>.from(_draftManualPalette);
    _manualPaletteWriteTimer = Timer(_manualPaletteDebounce, () async {
      try {
        await _repo.setManualPalette(committed);
        _savedManualPalette = committed;
        _currentPaletteKnown = true;
        _bumpState();
      } catch (e, st) {
        debugPrint('WispNotifier.setManualPalette write failed: $e\n$st');
      }
    });
  }

  /// Commit the draft palette to the wisp. Updates the saved snapshot
  /// so [manualPaletteDirty] goes back to false on success. Throws on
  /// the underlying BLE write failure (no optimism -- UI surfaces the
  /// error and leaves the draft in place so the user can retry).
  Future<void> setManualPalette() async {
    final committed = List<LampColor>.from(_draftManualPalette);
    try {
      await _repo.setManualPalette(committed);
      _savedManualPalette = committed;
      _currentPaletteKnown = true;
      _bumpState();
    } catch (e, st) {
      debugPrint('WispNotifier.setManualPalette() failed: $e\n$st');
      rethrow;
    }
  }

  /// Force a state-rebuild so consumers redraw. We don't have any new
  /// WispStatus to publish (the palette state is held outside it), but
  /// rebroadcasting the existing value is enough to nudge widgets that
  /// read [draftManualPalette] / [manualPaletteDirty] via the notifier.
  ///
  /// PERF (audit perf-H7, deferred): this defeats Riverpod's equality
  /// dedup -- `AsyncData(cur)` is a new wrapper around the same value,
  /// so consumers that `ref.watch` rebuild even when nothing they read
  /// has changed. The clean fix is to move `_draftManualPalette` into
  /// its own `StateProvider` so consumers can `.select` to just the
  /// palette slice. That refactor's wider than this remediation pass --
  /// the gradient-bar resolution drop (256 -> 30, this commit) cuts the
  /// downstream cost so the visible jank is gone even with the
  /// over-broad notify.
  void _bumpState() {
    final hadValue = state.value != null;
    final cur = state.value ?? WispStatus.empty;
    state = AsyncData(cur);
    debugPrint(
      '[wisp_notifier] _bumpState lamp=$lampId hadValue=$hadValue '
      'wispMac=${cur.wispMac} present=${cur.present}',
    );
  }

}
