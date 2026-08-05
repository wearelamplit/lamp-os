import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show debugPrint;
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../control/domain/lamp_color.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/uuids.dart';
import '../data/wisp_password_store.dart';
import '../data/wisp_repository.dart';
import '../domain/zone_source.dart';
import 'manual_palette_draft.dart';
import '../domain/wisp_source_mode.dart';
import '../domain/wisp_status.dart';

part 'wisp_notifier.g.dart';

/// Result of awaiting a manual-palette write confirmation on the status
/// NOTIFY. `sourceChanged` distinguishes "the wisp left Manual mid-confirm"
/// (the write is moot) from a genuine `notConfirmed` timeout.
enum PaletteConfirmOutcome { confirmed, notConfirmed, sourceChanged }

/// Owns the live [WispStatus] for a single lamp. On build it does one
/// read of `CHAR_WISP_STATUS` and subscribes to its notify stream;
/// thereafter every wispStatus update from the wisp lands in `state`
/// without a round-trip.
///
/// `setZone` / `clearZone` delegate to the repository and rely on the
/// wisp's on-change broadcast (≤ ~2s) to push the updated status back
/// via CHAR_WISP_STATUS. Optimistically reflects the choice in local
/// state so the chip highlight doesn't lag the tap; the notify either
/// confirms or corrects it.
@Riverpod(name: 'wispNotifierProvider')
class WispNotifier extends _$WispNotifier {
  StreamSubscription<Uint8List>? _sub;
  late WispRepository _repo;
  final _store = WispPasswordStore();
  // In-memory password cache; seeded from WispPasswordStore on build once
  // the wispMac is known.
  String? _cachedPassword;

  /// Debounce window for the realtime wispOp writes that fire on every
  /// Off-mode color-picker drag tick or manual-palette editor mutation.
  /// 250 ms gives a continuous picker drag ~4 writes/sec: fast enough
  /// to feel realtime, slow enough that NVS wear and BLE bandwidth stay
  /// reasonable.
  static const Duration _offColorDebounce =
      Duration(milliseconds: 250);
  static const Duration _driftDebounce = Duration(milliseconds: 300);
  static const Duration _brightnessDebounce = Duration(milliseconds: 250);

  /// A control op the wisp can silently drop at the BLE write or the mesh
  /// relay. Rather than fire-and-forget, resend up to this many times until
  /// the wisp echoes the change back. The 4×5=20s budget spans the wisp's
  /// ~20s post-change status re-broadcast burst, so a copy clears a coex
  /// blackout; it stays under [_sourceWriteGuard] so the guard can't lapse
  /// mid-retry.
  static const int _writeConfirmAttempts = 4;
  static const Duration _sourceConfirmTimeout = Duration(seconds: 5);
  Timer? _offColorWriteTimer;
  Timer? _driftWriteTimer;
  Timer? _brightnessWriteTimer;
  (int, int)? _pendingDrift; // (intervalMs, fadePct)
  int? _pendingBrightness;

  /// Optimistic-write guard for [setSource]. The wisp's response chain
  /// is: app BLE-writes wispOp → relay lamp forwards via MSG_CONTROL_OP
  /// → wisp dispatches setSource → triggerOnChange emits new wispStatus
  /// → wispStatus relays back to lamps → BLE notify fires. During that
  /// round-trip (up to seconds, especially via multi-hop relay), the
  /// relay lamp keeps notifying its CACHED wispStatus, still carrying
  /// the previous `source`. Adopting one would flip the picker back and,
  /// worse, replace the palette with the stale mode's contents (OFF →
  /// stale Manual with an empty palette). While the guard is armed, a
  /// status whose `source` doesn't match the pending mode is stale: it's
  /// dropped whole, not adopted. The guard releases when the wisp
  /// confirms (a status whose source == pending), and only falls back to
  /// the timeout if no confirmation arrives. The timeout sits well past a
  /// multi-hop round-trip so it can't strand the picker in optimistic
  /// state, then lets ground truth through so the user can retry.
  static const Duration _sourceWriteGuard = Duration(seconds: 22);
  WispSourceMode? _pendingSourceMode;
  DateTime? _pendingSourceUntil;

  // Bumped on every setSource. A superseded call reads its captured epoch back
  // as stale and stops resending, so an older mode's late resend can't land
  // after a newer one.
  int _setSourceEpoch = 0;

  // Palette comes from the dedicated `wisppalette` page section, not the
  // status char (whose palette-less NOTIFY would clobber it). A changed
  // `paletteIdPrefix` in a NOTIFY triggers a fresh section read.
  // _savedManualPalette: last committed. The in-flight editor draft lives in
  // manualPaletteDraftProvider so swatch edits rebuild watchers directly.
  List<LampColor> _savedManualPalette = const <LampColor>[];

  bool _manualPaletteWriteFailed = false;

  List<LampColor> get _draft => ref.read(manualPaletteDraftProvider(lampId));
  void _setDraft(List<LampColor> colors) =>
      ref.read(manualPaletteDraftProvider(lampId).notifier).set(colors);

  bool _currentPaletteKnown = false;
  String _lastPaletteIdPrefix = '';
  bool _paletteRereadInFlight = false;
  PaletteConfirmOutcome _paletteConfirmOutcome =
      PaletteConfirmOutcome.notConfirmed;

  Set<String>? _claimedMacs;
  Map<String, ({LampColor base, LampColor shade})> _livePaint = const {};
  bool _claimsReadInFlight = false;
  DateTime? _lastClaimsReadAt;
  DateTime? _lastStatusPollAt;

  /// True from a shuffle tap until a claims read shows [_livePaint] actually
  /// changed, so the preview can flag the residual mesh-relay lag instead of
  /// silently showing pre-shuffle colors. Bounded by [_recalcFallback] so a
  /// shuffle that produces no visible change (e.g. no claimed lamps) can't
  /// hang the indicator.
  static const Duration _recalcFallback = Duration(seconds: 8);
  bool _colorsRecalculating = false;
  Map<String, ({LampColor base, LampColor shade})>? _shuffleSnapshot;
  Timer? _recalcFallbackTimer;

  /// True while a shuffle's new colors haven't propagated back through
  /// CHAR_WISP_CLAIMS yet. Drives a "recalculating" hint on the preview.
  bool get colorsRecalculating => _colorsRecalculating;

  /// The set of lamp mesh macs the wisp currently claims, or null while
  /// the first CHAR_WISP_CLAIMS read is in flight. Empty set means the
  /// wisp reported count=0 (no claims / stale).
  Set<String>? get claimedMacs => _claimedMacs;

  /// Live paint colors keyed by uppercase colon-hex MAC, as broadcast by the
  /// wisp and relayed via CHAR_WISP_CLAIMS. A MAC absent from this map has
  /// no live color yet; callers fall back to predictTuple.
  Map<String, ({LampColor base, LampColor shade})> get livePaint => _livePaint;

  /// The currently-saved manual palette (last committed). Empty before
  /// the first save in a session.
  List<LampColor> get savedManualPalette => _savedManualPalette;

  /// True when the last manual-palette write exhausted its retries without
  /// the wisp echoing it back. Drives the editor's "not saved" indicator;
  /// cleared once a later write confirms or a fresh edit is scheduled.
  bool get manualPaletteWriteFailed => _manualPaletteWriteFailed;

  /// The in-flight editor draft. Mutates as the user adds, edits, reorders
  /// or deletes swatches; flushed to the wisp by [setManualPalette]. Widgets
  /// should watch [manualPaletteDraftProvider] directly to rebuild on edit.
  List<LampColor> get draftManualPalette => _draft;

  /// True when the editor has unsaved changes. Drives the save button's
  /// enabled state in the UI.
  bool get manualPaletteDirty {
    final draft = _draft;
    if (draft.length != _savedManualPalette.length) return true;
    for (var i = 0; i < draft.length; i++) {
      if (draft[i] != _savedManualPalette[i]) return true;
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
    _cachedPassword = null;
    _repo = WispRepository(ble, lampId);

    _currentPaletteKnown = false;
    _lastPaletteIdPrefix = '';
    _paletteRereadInFlight = false;

    _claimedMacs = null;
    _livePaint = const {};
    _claimsReadInFlight = false;
    _lastClaimsReadAt = null;
    _lastStatusPollAt = null;
    _colorsRecalculating = false;
    _shuffleSnapshot = null;

    ref.onDispose(() {
      _disposed = true;
      _sub?.cancel();
      _offColorWriteTimer?.cancel();
      _driftWriteTimer?.cancel();
      _brightnessWriteTimer?.cancel();
      _recalcFallbackTimer?.cancel();
    });

    debugPrint('[wisp_notifier] build lamp=$lampId -- waiting for connect');

    // Wait until connected before subscribing or reading.
    // `watchConnected` emits the current state on listen; this awaits the
    // first `true` to avoid racing the GATT layer with a BleNotFound.
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

    // Subscribe before the initial read to never miss a notify that
    // fires between the read returning and the listener attaching.
    _sub = ble
        .subscribe(lampId, BleUuids.controlService, BleUuids.wispStatus)
        .listen((bytes) {
      if (_disposed) return;
      var next = WispStatus.fromBytes(bytes);
      // A frame that decodes to "no wisp" while already holding a present
      // status is a transient empty/short BLE notify. Drop it to prevent
      // the icon, source, and palette from blinking out for a tick.
      if (!next.present && (state.value?.present ?? false)) {
        return;
      }
      final rawMac = next.wispMac;
      final guarded = _applySourceWriteGuard(next);
      if (guarded == null) {
        debugPrint(
          '[wisp_notifier] notify lamp=$lampId dropped stale source echo '
          'source=${next.source} pending=$_pendingSourceMode',
        );
        return;
      }
      next = guarded;
      _maybeRereadForPalette(next);
      // Throttle: claims change rarely; re-read at most once per ~3 s so this
      // doesn't issue a BLE read on every ~2 s wispStatus notify.
      final notifyNow = DateTime.now();
      final lastRead = _lastClaimsReadAt;
      if (_colorsRecalculating ||
          lastRead == null ||
          notifyNow.difference(lastRead) >= const Duration(seconds: 3)) {
        _lastClaimsReadAt = notifyNow;
        unawaited(_loadClaims());
      }
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
      // pre-auth read comes back empty (present == false). Retry briefly to
      // cover that window. Kept tight (2x1.5s) so a genuinely wisp-less lamp
      // resolves fast rather than sitting in loading; the notify subscription
      // (already attached above) catches anything slower.
      var initial = _applySourceWriteGuard(await _repo.readStatus()) ??
          WispStatus.empty;
      if (_disposed) return WispStatus.empty;
      for (var attempt = 0; !initial.present && attempt < 2; attempt++) {
        await Future.delayed(const Duration(milliseconds: 1500));
        if (_disposed) return WispStatus.empty;
        // A notify may have landed a present status during the wait; prefer it
        // so the retry's stale empty read can't clobber it.
        final live = state.value;
        if (live != null && live.present) {
          initial = live;
          break;
        }
        initial = _applySourceWriteGuard(await _repo.readStatus()) ??
            WispStatus.empty;
        if (_disposed) return WispStatus.empty;
      }
      _lastPaletteIdPrefix = initial.paletteIdPrefix;
      // Palette rides its own reliable section read, decoupled from the
      // notify-clobbered status char. Awaited before _loadClaims so the two
      // BLE reads don't collide on FbpBleClient's lockless flow.
      await _readManualPaletteSection(bumpState: false);
      // Seed the in-memory password cache from persistent storage.
      final mac = initial.wispMac;
      if (mac != null) {
        try {
          _cachedPassword = await _store.load(mac);
          _repo.updatePassword(_cachedPassword);
        } catch (_) {
          // SharedPreferences unavailable (test env without mock setup);
          // password stays null, ops go plaintext.
        }
      }
      unawaited(_loadClaims());
      debugPrint(
        '[wisp_notifier] initial-read lamp=$lampId '
        'wispMac=${initial.wispMac} present=${initial.present} '
        'source=${initial.source}',
      );
      return initial;
    } catch (e) {
      // GATT can still fail after connected==true if the link drops.
      // Return empty so the UI doesn't hang; the notify subscription catches
      // it up when the next status broadcast lands.
      debugPrint(
        '[wisp_notifier] initial-read FAILED lamp=$lampId -- '
        'returning empty. err=$e',
      );
      return WispStatus.empty;
    }
  }

  /// Read the wisp's manual palette from its dedicated section and seed the
  /// editor. A completed read marks the palette known even when empty, so the
  /// editor renders a populatable palette instead of hanging on "reading from
  /// wisp" forever. An empty / absent / errored section (legacy lamp, coex gap)
  /// leaves any prior palette intact; [WispRepository.readManualPalette] never
  /// throws. Serialized against [_loadClaims] via the in-flight flags:
  /// FbpBleClient has no internal lock, so two concurrent reads stall the flow.
  /// [bumpState] re-emits after ingesting so a notify-triggered re-read paints
  /// the new palette. Off during build: emitting there resolves the provider's
  /// future early, letting an optimistic write land before build's return
  /// clobbers it.
  Future<void> _readManualPaletteSection({bool bumpState = true}) async {
    if (_paletteRereadInFlight || _claimsReadInFlight || _disposed) return;
    _paletteRereadInFlight = true;
    try {
      final palette = await _repo.readManualPalette();
      if (_disposed) return;
      _currentPaletteKnown = true;
      _ingestManualPalette(palette);
      if (bumpState) _bumpState();
    } finally {
      _paletteRereadInFlight = false;
    }
  }

  /// Seed the editor from a freshly-read palette. Empty leaves the saved
  /// palette untouched. Doesn't touch the draft if the user is mid-edit (draft
  /// diverged from the previous saved); the next save replaces the wisp's view.
  void _ingestManualPalette(List<LampColor> palette) {
    if (palette.isEmpty) return;
    if (_palettesEqual(_savedManualPalette, palette)) return;
    final wasDraftPristine =
        _draft.isEmpty || _palettesEqual(_draft, _savedManualPalette);
    _savedManualPalette = List<LampColor>.unmodifiable(palette);
    if (wasDraftPristine) {
      _setDraft(_savedManualPalette);
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
      final parsed = await _repo.readClaims();
      if (_disposed) return;
      _claimedMacs = parsed?.macs;
      _livePaint = parsed?.colors ?? const {};
      final snapshot = _shuffleSnapshot;
      if (_colorsRecalculating &&
          snapshot != null &&
          !_livePaintEqual(_livePaint, snapshot)) {
        _colorsRecalculating = false;
        _shuffleSnapshot = null;
        _recalcFallbackTimer?.cancel();
        _recalcFallbackTimer = null;
      }
      _bumpState();
    } finally {
      _claimsReadInFlight = false;
    }
  }

  static bool _livePaintEqual(
    Map<String, ({LampColor base, LampColor shade})> a,
    Map<String, ({LampColor base, LampColor shade})> b,
  ) {
    if (a.length != b.length) return false;
    for (final e in a.entries) {
      if (b[e.key] != e.value) return false;
    }
    return true;
  }

  // The small, reliable status NOTIFY carries paletteIdPrefix; a changed
  // prefix means the wisp's palette moved, so re-read the palette section.
  // Don't consume the prefix while a read is busy; retry on the next notify.
  void _maybeRereadForPalette(WispStatus next) {
    final prefix = next.paletteIdPrefix;
    if (prefix.isEmpty || prefix == _lastPaletteIdPrefix) return;
    if (_paletteRereadInFlight || _claimsReadInFlight) return;
    _lastPaletteIdPrefix = prefix;
    unawaited(_readManualPaletteSection());
  }

  /// Pin the wisp to [zoneId]. Optimistically updates local state to
  /// `zoneSource: appOp` so the UI feels responsive; the next status
  /// notify will replace this with the wisp's authoritative view.
  ///
  /// On BLE write failure, rolls the optimistic state back to its
  /// pre-call snapshot before rethrowing. Without the rollback the user sees
  /// the chip "stick" on the failed selection even though the wisp never
  /// received the op, with no notify coming to reconcile it.
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
      if (_disposed) rethrow;
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
      if (_disposed) rethrow;
      state = prev;
      rethrow;
    }
  }

  /// Push WiFi credentials to the wisp. The wisp persists into NVS and
  /// kicks `WifiLink::reconnect()`; connection state surfaces back through
  /// `WispStatus.wifiConnected` on the next status notify.
  ///
  /// No optimistic flip of `wifiConnected`: the wisp may fail to associate
  /// (wrong password, AP out of range), so the badge reflects only what
  /// the wisp reports. The UI tracks "save sent" via its own local flag.
  Future<void> setWifi(String ssid, String password) async {
    try {
      await _repo.setWifi(ssid, password);
    } catch (e, st) {
      debugPrint('WispNotifier.setWifi() failed: $e\n$st');
      rethrow;
    }
  }

  /// Ask the wisp to re-attempt WiFi with its stored credentials, clearing a
  /// channel-mismatch rejection. The wisp's connection state surfaces back
  /// through `WispStatus` on the next status notify; no optimistic flip here.
  Future<void> wifiReconnect() async {
    try {
      await _repo.wifiReconnect();
    } catch (e, st) {
      debugPrint('WispNotifier.wifiReconnect() failed: $e\n$st');
      rethrow;
    }
  }

  /// Pick the color the wisp renders on its own 30-pixel ring while
  /// sourceMode is Off. Off mode keeps PaintDistributor held off;
  /// this color exists only on the wisp ring. Optimistically reflects
  /// in local state; BLE write is debounced at [_offColorDebounce]
  /// (~4 writes/sec) to bound NVS wear.
  ///
  /// No rollback on failure; the wisp's next status notify corrects
  /// local state if the write missed.
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

  /// Set the wisp's human-readable name. No optimistic state: the wisp
  /// echoes the new name back through wispStatus on the next notify.
  Future<void> setName(String name) async {
    try {
      await _repo.setName(name);
    } catch (e, st) {
      debugPrint('WispNotifier.setName() failed: $e\n$st');
      rethrow;
    }
  }

  /// Set or change the wisp password.
  ///
  /// Factory-fresh ([currentPassword] null): the op is plaintext, persisted on
  /// write success. Changing an existing password ([currentPassword] set): the
  /// op is sealed under the entered current password, and the new password is
  /// persisted ONLY after the wisp confirms acceptance by advancing `opSeq`.
  /// Returns true on success; false when the current password was wrong (the
  /// store and cache are left untouched).
  Future<bool> setPassword(String newPassword, {String? currentPassword}) async {
    final mac = state.value?.wispMac;
    if (currentPassword == null) {
      try {
        await _repo.setPassword(newPassword);
      } catch (e, st) {
        debugPrint('WispNotifier.setPassword() failed: $e\n$st');
        rethrow;
      }
      _cachedPassword = newPassword;
      _repo.updatePassword(newPassword);
      if (mac != null) await _store.save(mac, newPassword);
      return true;
    }
    final ok = await _sendVerifiedPasswordOp(
      currentGuess: currentPassword,
      sendOp: () => _repo.setPassword(newPassword),
    );
    if (!ok) return false;
    _cachedPassword = newPassword;
    _repo.updatePassword(newPassword);
    if (mac != null) await _store.save(mac, newPassword);
    return true;
  }

  /// Remove the wisp password. The clear op is sealed under [currentPassword]
  /// and only persisted once the wisp confirms acceptance via an `opSeq`
  /// advance. Returns true on success; false when the current password was
  /// wrong (store and cache untouched).
  Future<bool> clearPassword(String currentPassword) async {
    final mac = state.value?.wispMac;
    final ok = await _sendVerifiedPasswordOp(
      currentGuess: currentPassword,
      sendOp: () => _repo.setPassword(''),
    );
    if (!ok) return false;
    _cachedPassword = null;
    _repo.updatePassword(null);
    if (mac != null) await _store.clear(mac);
    return true;
  }

  /// How long to wait for the wisp's `opSeq` to advance after a sealed
  /// password op. A wrong current password fails the seal on the wisp, so no
  /// advance arrives and the window elapses.
  static const Duration _opConfirmTimeout = Duration(seconds: 5);

  /// Seal [sendOp] under [currentGuess] and wait for the wisp to confirm it
  /// applied by advancing `opSeq`. Restores the previously-cached password on
  /// failure so a wrong guess doesn't strand the repo on a bad secret.
  Future<bool> _sendVerifiedPasswordOp({
    required String currentGuess,
    required Future<void> Function() sendOp,
  }) async {
    final baseline = state.value?.opSeq ?? 0;
    final prevCached = _cachedPassword;
    // Watch for the confirming status before sending so a fast echo can't slip
    // through between the write and the listen.
    final confirmed = _awaitOpSeqAdvance(baseline);
    _repo.updatePassword(currentGuess.isEmpty ? null : currentGuess);
    try {
      await sendOp();
    } catch (e, st) {
      debugPrint('WispNotifier verified password op send failed: $e\n$st');
      _repo.updatePassword(prevCached);
      unawaited(confirmed.catchError((_) => false));
      rethrow;
    }
    try {
      await _repo.pollStatus();
    } catch (_) {}
    final ok = await confirmed;
    if (!ok) _repo.updatePassword(prevCached);
    return ok;
  }

  /// Resolve true once a wisp status arrives whose `opSeq` advances past
  /// [baseline], or false when [_opConfirmTimeout] elapses first. A reboot
  /// resets `opSeq` to 0, so only a strict advance counts as confirmation.
  Future<bool> _awaitOpSeqAdvance(int baseline) {
    return ref
        .read(bleClientProvider)
        .subscribe(lampId, BleUuids.controlService, BleUuids.wispStatus)
        .map((bytes) => WispStatus.fromBytes(bytes).opSeq)
        .firstWhere((seq) => seq > baseline)
        .timeout(_opConfirmTimeout)
        .then((_) => true)
        .catchError((_) => false);
  }

  /// Inject a known password into the in-memory cache and repository,
  /// and persist it. Called when the user enters the password via the UI
  /// or from tests.
  Future<void> cachePassword(String wispMac, String password) async {
    _cachedPassword = password;
    _repo.updatePassword(password);
    await _store.save(wispMac, password);
  }

  /// Rate limit for [pollStatus]: screen re-opens inside this window skip
  /// the write (the wisp coalesces on its side too).
  static const Duration _statusPollMinInterval = Duration(seconds: 4);

  /// Ask the wisp to re-broadcast its status now. Fired on wisp-screen
  /// open so a stale relay cache refreshes without waiting for the 30 s
  /// heartbeat. Failures are swallowed: the heartbeat covers it.
  Future<void> pollStatus() async {
    final now = DateTime.now();
    final last = _lastStatusPollAt;
    if (last != null && now.difference(last) < _statusPollMinInterval) return;
    _lastStatusPollAt = now;
    try {
      await _repo.pollStatus();
    } catch (e) {
      debugPrint('WispNotifier.pollStatus failed: $e');
    }
  }

  /// Shuffle: tell the wisp to bump its seed and re-roll per-lamp color
  /// assignments. The updated seed rides back in the next wispStatus so
  /// the app preview re-rolls in lock-step without optimistic state here.
  Future<void> shuffle() async {
    _shuffleSnapshot = Map.of(_livePaint);
    _colorsRecalculating = true;
    _recalcFallbackTimer?.cancel();
    _recalcFallbackTimer = Timer(_recalcFallback, () {
      if (_disposed) return;
      _colorsRecalculating = false;
      _shuffleSnapshot = null;
      _bumpState();
    });
    _bumpState();
    try {
      await _repo.shuffle();
    } catch (e, st) {
      debugPrint('WispNotifier.shuffle() failed: $e\n$st');
      rethrow;
    }
    if (_disposed) return;
    // Bypass the notify throttle so the re-rolled colors are caught as soon as
    // they reach CHAR_WISP_CLAIMS.
    _lastClaimsReadAt = null;
    unawaited(_loadClaims());
  }

  /// Configure the wisp LED strip byte order and pixel count. Optimistically
  /// reflects in local state; the wisp echoes the applied config back through
  /// the next wispStatus notify.
  Future<void> setLedStrip(String ledType, int pixelCount) async {
    final cur = state.value ?? WispStatus.empty;
    state = AsyncData(cur.copyWith(ledType: ledType, pixelCount: pixelCount));
    try {
      await _repo.setLedStrip(ledType, pixelCount);
    } catch (e, st) {
      debugPrint('WispNotifier.setLedStrip write failed: $e\n$st');
    }
  }

  /// Debounce slider drags to one write per [_driftDebounce] window.
  void setDrift(int intervalMs, int fadePct) {
    _pendingDrift = (intervalMs, fadePct);
    _driftWriteTimer?.cancel();
    _driftWriteTimer = Timer(_driftDebounce, () => unawaited(_flushDrift()));
  }

  /// Call on slider release so the final value lands without waiting for the
  /// debounce window.
  void flushDrift() {
    _driftWriteTimer?.cancel();
    _driftWriteTimer = null;
    unawaited(_flushDrift());
  }

  Future<void> _flushDrift() async {
    final pending = _pendingDrift;
    if (pending == null) return;
    _pendingDrift = null;
    try {
      await _repo.setDrift(pending.$1, pending.$2);
    } catch (e, st) {
      debugPrint('WispNotifier.setDrift write failed: $e\n$st');
    }
  }

  /// Set the space-brightness factor (0..100). Optimistically reflects in
  /// local state so the slider tracks the drag, then debounces the BLE write
  /// at [_brightnessDebounce] to bound NVS wear. The wisp echoes the applied
  /// value back through the next wispStatus notify.
  void setBrightness(int pct) {
    final cur = state.value ?? WispStatus.empty;
    state = AsyncData(cur.copyWith(brightness: pct));
    _pendingBrightness = pct;
    _brightnessWriteTimer?.cancel();
    _brightnessWriteTimer =
        Timer(_brightnessDebounce, () => unawaited(_flushBrightness()));
  }

  /// Call on slider release so the final value lands without waiting for the
  /// debounce window.
  void flushBrightness() {
    _brightnessWriteTimer?.cancel();
    _brightnessWriteTimer = null;
    unawaited(_flushBrightness());
  }

  Future<void> _flushBrightness() async {
    final pending = _pendingBrightness;
    if (pending == null) return;
    _pendingBrightness = null;
    try {
      await _repo.setBrightness(pending);
    } catch (e, st) {
      debugPrint('WispNotifier.setBrightness write failed: $e\n$st');
    }
  }

  /// Set the wisp source mode (Off / Manual / Aurora).
  /// Optimistically reflects in local state so the pill picker doesn't
  /// lag the tap, then resends until the wisp's wispStatus echoes the mode
  /// back (the op can be dropped at the BLE write or the mesh relay).
  /// Throws when every attempt goes unconfirmed so the caller can surface it;
  /// rolls the optimistic state back first.
  ///
  /// Arms the source-write guard so stale wispStatus echoes from the
  /// relay lamp between the BLE write and the wisp's triggerOnChange
  /// response don't flip the picker back. See [_sourceWriteGuard].
  Future<void> setSource(WispSourceMode mode) async {
    // An op for this exact mode is already optimistically applied and
    // resending on its own confirm budget. Re-issuing spawns a second loop
    // whose stale `prev` snapshot can clobber the newer optimistic state on
    // rollback. A different mode still supersedes via the epoch path below.
    if (_pendingSourceMode == mode) return;
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
    final epoch = ++_setSourceEpoch;

    final landed = await _sendConfirmed(
      send: () => _repo.setSource(mode),
      confirm: () => _confirmSourceLanded(mode),
      isSuperseded: () => _setSourceEpoch != epoch,
    );
    if (_disposed) return;
    // The confirming echo already released the guard and adopted the status.
    if (landed) return;
    // A newer setSource took over while this one retried; leave its guard.
    if (_pendingSourceMode != mode) return;

    // Roll optimistic state back and drop the guard so ground truth shows.
    _pendingSourceMode = null;
    _pendingSourceUntil = null;
    state = prev;
    throw Exception('wisp setSource($mode) not confirmed');
  }

  /// Resolve true once a wispStatus arrives whose `source` matches [mode]
  /// (the wisp applied and re-broadcast the change), or false when
  /// [_sourceConfirmTimeout] elapses. Watch the notify stream directly:
  /// every wispStatus frame carries `source`.
  Future<bool> _confirmSourceLanded(WispSourceMode mode) {
    return ref
        .read(bleClientProvider)
        .subscribe(lampId, BleUuids.controlService, BleUuids.wispStatus)
        .map((bytes) => WispStatus.fromBytes(bytes))
        .firstWhere((s) => s.present && s.source == mode)
        .timeout(_sourceConfirmTimeout)
        .then((_) => true)
        .catchError((_) => false);
  }

  /// Send an op, then wait for [confirm] to resolve true within its own
  /// timeout, resending up to [_writeConfirmAttempts] on a miss. [confirm]
  /// is armed before [send] each attempt so a fast echo isn't missed.
  /// Returns true once confirmed, false when every attempt goes unconfirmed.
  /// [isSuperseded], when supplied and true at the top of an attempt, stops
  /// resending immediately and returns false: a newer op now owns the state,
  /// so a stale resend from this loop must not reach the wisp.
  Future<bool> _sendConfirmed({
    required Future<void> Function() send,
    required Future<bool> Function() confirm,
    bool Function()? isSuperseded,
  }) async {
    for (var attempt = 0;
        attempt < _writeConfirmAttempts && !_disposed;
        attempt++) {
      if (isSuperseded != null && isSuperseded()) return false;
      Future<bool>? confirmed;
      try {
        confirmed = confirm();
        await send();
        if (await confirmed) return true;
      } catch (e, st) {
        debugPrint('WispNotifier confirmed-op attempt failed: $e\n$st');
        if (confirmed != null) {
          unawaited(confirmed.then((_) {}, onError: (_) {}));
        }
      }
    }
    return false;
  }

  /// Apply the [setSource] optimistic-write guard to an incoming
  /// [WispStatus]. Returns null when the status is a stale pre-write
  /// echo that the caller must drop whole; otherwise returns the status
  /// to adopt. The guard releases (and lets the status through) when the
  /// wisp confirms (incoming.source == pending) or the fallback window
  /// expires.
  WispStatus? _applySourceWriteGuard(WispStatus incoming) {
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
    return null;
  }

  // Each helper emits a fresh draft list so the provider's watchers rebuild.
  // Editing is local preview only; the draft reaches the wisp on an explicit
  // [setManualPalette] (Save), never per edit.

  /// Seed the draft from the saved snapshot. Called by the UI on first
  /// open of the editor so the swatches reflect what was last committed.
  void resetManualPaletteDraft() {
    _setDraft(_savedManualPalette);
  }

  /// Replace the whole draft palette in one shot (the color-stops editor
  /// streams the full list per edit). Caps at 10; extra swatches are dropped.
  void setManualPaletteDraft(List<LampColor> colors) {
    final capped = colors.length > 10 ? colors.sublist(0, 10) : colors;
    if (_palettesEqual(_draft, capped)) return;
    _setDraft(capped);
  }

  /// Append a swatch to the draft. Caps at 10 -- anything beyond is
  /// silently ignored so the UI's `+` button can be permissive.
  void appendManualPaletteColor(LampColor color) {
    final draft = _draft;
    if (draft.length >= 10) return;
    _setDraft([...draft, color]);
  }

  /// Replace the swatch at [index]. No-op if [index] is out of range.
  void updateManualPaletteColor(int index, LampColor color) {
    final draft = _draft;
    if (index < 0 || index >= draft.length) return;
    if (draft[index] == color) return;
    final next = List<LampColor>.from(draft);
    next[index] = color;
    _setDraft(next);
  }

  /// Remove the swatch at [index] (swipe-to-delete).
  void removeManualPaletteColor(int index) {
    final draft = _draft;
    if (index < 0 || index >= draft.length) return;
    final next = List<LampColor>.from(draft);
    next.removeAt(index);
    _setDraft(next);
  }

  /// Drag-to-reorder. Standard "move item at [oldIndex] to [newIndex]"
  /// semantics -- newIndex is the index in the list AFTER removal.
  void reorderManualPaletteColor(int oldIndex, int newIndex) {
    final draft = _draft;
    if (oldIndex < 0 || oldIndex >= draft.length) return;
    final next = List<LampColor>.from(draft);
    final item = next.removeAt(oldIndex);
    final clampedNew = newIndex.clamp(0, next.length);
    next.insert(clampedNew, item);
    if (_palettesEqual(next, draft)) return;
    _setDraft(next);
  }

  /// Commit the draft palette to the wisp (explicit Save). Updates the saved
  /// snapshot so [manualPaletteDirty] goes back to false once the wisp confirms
  /// it stored the palette. Sends once and awaits the confirm/retry loop; sets
  /// [manualPaletteWriteFailed] and throws when it can't be confirmed. The Save
  /// caller fires this without awaiting the confirm, so the throw is swallowed
  /// and the failure surfaces via the flag.
  Future<void> setManualPalette() async {
    final committed = List<LampColor>.from(_draft);
    if (committed.isEmpty) {
      // Wisp keeps its fallback on an empty palette and never stamps the empty
      // hash, so there's no status prefix to await -- treat as landed.
      _applyPaletteConfirm(committed, true);
      return;
    }
    final expectedHash = WispRepository.paletteIdHash(committed);
    _paletteConfirmOutcome = PaletteConfirmOutcome.notConfirmed;
    final ok = await _sendConfirmed(
      send: () => _repo.setManualPalette(committed),
      confirm: () => _confirmPaletteLanded(expectedHash),
    );
    if (_disposed) return;
    _applyPaletteConfirm(committed, ok);
    if (!ok) {
      if (_paletteConfirmOutcome == PaletteConfirmOutcome.sourceChanged) {
        throw Exception('wisp manual palette write source changed');
      }
      throw Exception('wisp manual palette write not confirmed');
    }
  }

  void _applyPaletteConfirm(List<LampColor> committed, bool ok) {
    if (ok) {
      _savedManualPalette = List<LampColor>.unmodifiable(committed);
      _currentPaletteKnown = true;
      _manualPaletteWriteFailed = false;
    } else {
      _manualPaletteWriteFailed = true;
    }
    _bumpState();
  }

  /// Resolve true once a wispStatus arrives whose `paletteIdPrefix` matches
  /// [expectedHash] (the wisp stored and re-broadcast this exact palette), or
  /// false when [_sourceConfirmTimeout] elapses. Mirrors [_confirmSourceLanded]:
  /// every wispStatus NOTIFY carries the palette id prefix, so no READ-poll.
  ///
  /// If the wisp's `source` leaves Manual (or a local [_pendingSourceMode]
  /// already did) the palette write is moot; records
  /// [PaletteConfirmOutcome.sourceChanged] so the caller can surface a
  /// distinct error instead of a generic "not confirmed".
  Future<bool> _confirmPaletteLanded(String expectedHash) {
    final localFlip = _pendingSourceMode != null &&
        _pendingSourceMode != WispSourceMode.manual;
    // sourceChanged is terminal within a commit: once seen, don't let a later
    // retry's timeout downgrade it to a generic notConfirmed.
    if (localFlip ||
        _paletteConfirmOutcome == PaletteConfirmOutcome.sourceChanged) {
      _paletteConfirmOutcome = PaletteConfirmOutcome.sourceChanged;
      return Future.value(false);
    }
    return ref
        .read(bleClientProvider)
        .subscribe(lampId, BleUuids.controlService, BleUuids.wispStatus)
        .map((bytes) => WispStatus.fromBytes(bytes))
        .firstWhere((s) =>
            s.present &&
            (s.paletteIdPrefix == expectedHash ||
                s.source != WispSourceMode.manual))
        .timeout(_sourceConfirmTimeout)
        .then((s) {
      if (s.source != WispSourceMode.manual) {
        _paletteConfirmOutcome = PaletteConfirmOutcome.sourceChanged;
        return false;
      }
      _paletteConfirmOutcome = PaletteConfirmOutcome.confirmed;
      return true;
    }).catchError((_) {
      _paletteConfirmOutcome = PaletteConfirmOutcome.notConfirmed;
      return false;
    });
  }

  /// Re-emit the held [WispStatus] to nudge consumers of the field-backed
  /// claims / livePaint snapshot after an out-of-band update.
  void _bumpState() {
    if (_disposed) return;
    final hadValue = state.value != null;
    final cur = state.value ?? WispStatus.empty;
    state = AsyncData(cur);
    debugPrint(
      '[wisp_notifier] _bumpState lamp=$lampId hadValue=$hadValue '
      'wispMac=${cur.wispMac} present=${cur.present}',
    );
  }

}
