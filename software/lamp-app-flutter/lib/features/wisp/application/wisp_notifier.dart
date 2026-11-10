import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show debugPrint, visibleForTesting;
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../control/domain/lamp_color.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/uuids.dart';
import '../data/wisp_password_store.dart';
import '../data/wisp_repository.dart';
import 'manual_palette_draft.dart';
import '../domain/wisp_config.dart';
import '../domain/wisp_source_mode.dart';
import '../domain/wisp_status.dart';

part 'wisp_notifier.g.dart';

/// Result of awaiting a manual-palette write confirmation on the status
/// NOTIFY. `sourceChanged` distinguishes "the wisp left Manual mid-confirm"
/// (the write is moot) from a genuine `notConfirmed` timeout.
enum PaletteConfirmOutcome { confirmed, notConfirmed, sourceChanged }

/// Owns the live [WispStatus] for a single lamp. On build it does one
/// status read (the `wispstatus` page section, or `CHAR_WISP_STATUS` on a
/// lamp without it) and subscribes to the status notify stream; thereafter
/// every wispStatus update from the wisp lands in `state` without a
/// round-trip. The wisp's settings come from the `wispconfig` section when
/// the lamp serves it, re-read whenever the status's `configLastSeenMs`
/// moves, and are merged over whatever the status carried.
///
/// `setOscRole` delegates to the repository and relies on the wisp's
/// on-change broadcast (≤ ~2s) to push the updated status back via
/// CHAR_WISP_STATUS. Optimistically reflects the choice in local state so
/// the picker doesn't lag the tap; the notify either confirms or corrects it.
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

  /// Sum of every resend's confirm window. An unconfirmed op surfaces failure
  /// after this long when each BLE write returns promptly.
  @visibleForTesting
  static Duration get writeConfirmBudget =>
      _sourceConfirmTimeout * _writeConfirmAttempts;

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

  // Bumped on every setManualPalette, the same way [_setSourceEpoch] is.
  int _paletteSaveEpoch = 0;

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

  // Latest wispConfig from the `wispconfig` section and the wisp it belongs
  // to; merged over the config fields of every status from that wisp.
  WispConfig? _config;
  String? _configMac;
  int? _lastConfigSeenMs;
  bool _configRereadInFlight = false;
  bool _statusRereadInFlight = false;
  DateTime? _lastConfigPollAt;

  // Every status the wisp sent, config merged, ahead of the source-write
  // guard. The confirm waits listen here so a status that only a section
  // re-read could recover still confirms. Only wisp statuses land here. The
  // held state can be optimistic, and a confirm read off it confirms itself.
  late StreamController<WispStatus> _incoming;

  // Every opSeq the wisp reports, from a status or a `wispconfig` read.
  late StreamController<int> _opSeqs;

  // FbpBleClient has no internal lock, so two concurrent characteristic
  // reads stall the GATT flow on Android; every read here skips while
  // another is in flight and retries on the next notify.
  bool get _readBusy =>
      _paletteRereadInFlight ||
      _claimsReadInFlight ||
      _configRereadInFlight ||
      _statusRereadInFlight;

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

    _config = null;
    _configMac = null;
    _lastConfigSeenMs = null;
    _configRereadInFlight = false;
    _statusRereadInFlight = false;
    _lastConfigPollAt = null;
    final incoming = _incoming = StreamController<WispStatus>.broadcast();
    final opSeqs = _opSeqs = StreamController<int>.broadcast();

    ref.onDispose(() {
      _disposed = true;
      _sub?.cancel();
      unawaited(incoming.close());
      unawaited(opSeqs.close());
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
      final next = WispStatus.fromBytes(bytes);
      if (next.configLastSeenMs != null) _repo.noteWispSections();
      // An empty or MTU-truncated notify on a lamp that serves the status
      // whole: re-read it there rather than drop the update.
      if (!next.present && _repo.wispSectionsSupported) {
        unawaited(_rereadStatusSection());
        return;
      }
      _onIncoming(next);
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
      if (_disposed) return WispStatus.empty;
      if (initial.present && _repo.wispSectionsSupported) {
        await _readConfigSection(initial, emit: false);
        if (_disposed) return WispStatus.empty;
        initial = _withHeldConfig(initial);
      }
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
    if (_readBusy || _disposed) return;
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
    if (_readBusy || _disposed) return;
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

  /// Adopt a status from a notify or a section re-read: merge the held
  /// config, publish it to the confirm waits, then apply the source-write
  /// guard and the palette / config / claims re-read triggers.
  void _onIncoming(WispStatus raw) {
    if (_disposed) return;
    var next = _withHeldConfig(raw);
    _incoming.add(next);
    _opSeqs.add(next.opSeq);
    // A frame that decodes to "no wisp" while already holding a present
    // status is a transient empty/short BLE notify. Drop it to prevent
    // the icon, source, and palette from blinking out for a tick.
    if (!next.present && (state.value?.present ?? false)) {
      return;
    }
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
    _maybeRereadConfig(next);
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
      '[wisp_notifier] notify lamp=$lampId '
      'wispMac=${next.wispMac} present=${next.present} source=${next.source}',
    );
  }

  Future<void> _rereadStatusSection() async {
    if (_readBusy || _disposed) return;
    _statusRereadInFlight = true;
    final WispStatus status;
    try {
      status = await _repo.readStatus();
    } catch (e) {
      debugPrint('[wisp_notifier] status re-read failed lamp=$lampId: $e');
      return;
    } finally {
      _statusRereadInFlight = false;
    }
    if (_disposed || !status.present) return;
    _onIncoming(status);
  }

  /// [status] with the held config merged in, when the status carries no
  /// settings of its own and the config came from the same wisp. A status
  /// that [WispStatus.carriesConfig] is the fresher of the two: a config
  /// re-read can be skipped or lost, a status cannot go stale behind it.
  WispStatus _withHeldConfig(WispStatus status) {
    final config = _config;
    if (config == null ||
        status.carriesConfig ||
        status.wispMac == null ||
        status.wispMac != _configMac) {
      return status;
    }
    return status.withConfig(config);
  }

  // Bounded wait for an in-flight read, so a caller that needs the section's
  // current contents reads it rather than skipping as busy.
  Future<void> _awaitReadsIdle() async {
    final deadline = DateTime.now().add(_readIdleTimeout);
    while (_readBusy && !_disposed && DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
  }

  static const Duration _readIdleTimeout = Duration(seconds: 3);

  // The lamp stamps configLastSeenMs when a wispConfig lands; a new stamp
  // means the section holds something to read. Busy reads leave the stamp
  // unconsumed so the next notify retries.
  void _maybeRereadConfig(WispStatus next) {
    final seen = next.configLastSeenMs;
    if (seen == null || seen == _lastConfigSeenMs) return;
    unawaited(_readConfigSection(next));
  }

  /// Read the `wispconfig` section for the wisp [forStatus] describes and
  /// hold it. With [emit], re-publish the current status with it merged.
  Future<void> _readConfigSection(
    WispStatus forStatus, {
    bool emit = true,
  }) async {
    if (_readBusy || _disposed) return;
    _configRereadInFlight = true;
    final WispConfig? config;
    try {
      config = await _repo.readConfig();
    } finally {
      _configRereadInFlight = false;
    }
    if (_disposed || config == null) return;
    _config = config;
    _configMac = forStatus.wispMac;
    _lastConfigSeenMs = forStatus.configLastSeenMs;
    if (!emit) return;
    final cur = state.value;
    if (cur == null) return;
    final merged = _withHeldConfig(cur);
    _opSeqs.add(merged.opSeq);
    state = AsyncData(merged);
  }

  // The small, reliable status NOTIFY carries paletteIdPrefix; a changed
  // prefix means the wisp's palette moved, so re-read the palette section.
  // Don't consume the prefix while a read is busy; retry on the next notify.
  void _maybeRereadForPalette(WispStatus next) {
    final prefix = next.paletteIdPrefix;
    if (prefix == _lastPaletteIdPrefix) return;
    // An empty prefix has no palette to read, but recording it lets the same
    // palette coming back register as a change.
    if (prefix.isEmpty) {
      _lastPaletteIdPrefix = prefix;
      return;
    }
    if (_readBusy) return;
    _lastPaletteIdPrefix = prefix;
    unawaited(_readManualPaletteSection());
  }

  /// Select the Straylight palette role. Optimistically updates local
  /// state; the next status notify replaces it with the wisp's view.
  ///
  /// On BLE write failure, rolls the optimistic state back to its pre-call
  /// snapshot before rethrowing, so the picker never sticks on a role the
  /// wisp never received.
  Future<void> setOscRole(OscRole role) async {
    final prev = state;
    final cur = state.value ?? WispStatus.empty;
    state = AsyncData(cur.copyWith(oscRole: role));
    try {
      await _repo.setOscRole(role);
    } catch (e, st) {
      debugPrint('WispNotifier.setOscRole($role) failed: $e\n$st');
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
    // opSeq rides the config: refresh it from the section first, waiting out
    // any read in flight, so the baseline is the wisp's current value. A
    // split status with no config to read has no baseline at all, and a
    // placeholder 0 would let any later opSeq confirm a rejected change.
    final cur = state.value;
    if (cur != null && _repo.wispSectionsSupported) {
      await _awaitReadsIdle();
      await _readConfigSection(state.value ?? cur);
    }
    final now = state.value;
    if (now != null && now.configSplit && !now.configKnown) {
      throw StateError('wisp settings not loaded; opSeq baseline unknown');
    }
    final baseline = now?.opSeq ?? 0;
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
    return _opSeqs.stream
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

  /// Ask the wisp to re-broadcast its wispConfig now, so the lamp's cached
  /// copy (and the settings forms) refresh on screen open. Same rate limit
  /// as [pollStatus]; failures are swallowed.
  Future<void> pollConfig() async {
    final now = DateTime.now();
    final last = _lastConfigPollAt;
    if (last != null && now.difference(last) < _statusPollMinInterval) return;
    _lastConfigPollAt = now;
    try {
      await _repo.pollConfig();
    } catch (e) {
      debugPrint('WispNotifier.pollConfig failed: $e');
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
    _config = _config?.copyWith(ledType: ledType, pixelCount: pixelCount);
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
    _config = _config?.copyWith(brightness: pct);
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

  /// Set the wisp source mode (Off / Manual / Osc).
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
  /// [_sourceConfirmTimeout] elapses. Every wispStatus carries `source`.
  Future<bool> _confirmSourceLanded(WispSourceMode mode) {
    return _incoming.stream
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
  /// and the failure surfaces via the flag. A later save supersedes this one,
  /// which then stops resending and returns without touching the saved
  /// snapshot.
  Future<void> setManualPalette() async {
    final committed = List<LampColor>.from(_draft);
    // The wisp stores an empty palette and clears its palette id, so a Manual
    // status that omits paletteIdPrefix confirms it.
    final expectedHash =
        committed.isEmpty ? '' : WispRepository.paletteIdHash(committed);
    final wispMac = state.value?.wispMac;
    final epoch = ++_paletteSaveEpoch;
    var outcome = PaletteConfirmOutcome.notConfirmed;
    final ok = await _sendConfirmed(
      send: () => _repo.setManualPalette(committed),
      confirm: () async {
        // sourceChanged is terminal within a save. A later retry's timeout
        // must not downgrade it to a generic notConfirmed.
        if (outcome != PaletteConfirmOutcome.sourceChanged) {
          outcome = await _confirmPaletteLanded(expectedHash, wispMac);
        }
        return outcome == PaletteConfirmOutcome.confirmed;
      },
      isSuperseded: () => _paletteSaveEpoch != epoch,
    );
    if (_disposed || _paletteSaveEpoch != epoch) return;
    _applyPaletteConfirm(committed, ok);
    if (!ok) {
      if (outcome == PaletteConfirmOutcome.sourceChanged) {
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

  /// Resolve [PaletteConfirmOutcome.confirmed] once a status from the wisp
  /// at [wispMac] arrives whose `paletteIdPrefix` equals [expectedHash],
  /// meaning the wisp stored and re-broadcast this exact palette, or
  /// [PaletteConfirmOutcome.notConfirmed] when [_sourceConfirmTimeout]
  /// elapses. An empty [expectedHash] matches a status that omits the
  /// prefix. Every wispStatus NOTIFY carries the palette id state, so no
  /// READ-poll is needed.
  ///
  /// If that wisp's `source` leaves Manual (or a local [_pendingSourceMode]
  /// already did) the palette write is moot and the outcome is
  /// [PaletteConfirmOutcome.sourceChanged].
  Future<PaletteConfirmOutcome> _confirmPaletteLanded(
    String expectedHash,
    String? wispMac,
  ) {
    final localFlip = _pendingSourceMode != null &&
        _pendingSourceMode != WispSourceMode.manual;
    if (localFlip) return Future.value(PaletteConfirmOutcome.sourceChanged);
    return _incoming.stream
        .firstWhere((s) =>
            s.present &&
            s.wispMac == wispMac &&
            (s.paletteIdPrefix == expectedHash ||
                s.source != WispSourceMode.manual))
        .timeout(_sourceConfirmTimeout)
        .then((s) => s.source == WispSourceMode.manual
            ? PaletteConfirmOutcome.confirmed
            : PaletteConfirmOutcome.sourceChanged)
        .catchError((_) => PaletteConfirmOutcome.notConfirmed);
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
