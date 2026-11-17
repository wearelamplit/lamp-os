import 'dart:async';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show debugPrint, visibleForTesting;
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../control/domain/lamp_color.dart';
import '../../../core/ble/ble_client.dart';
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
  StreamSubscription<bool>? _connSub;
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

  /// The first palette save `seq` this notifier sends, 1..65535. Random, so
  /// two phones rarely start on the same one.
  @visibleForTesting
  static int Function() debugSeqStart = () => Random().nextInt(_seqMax) + 1;
  static const int _seqMax = 65535;
  int? _lastSaveSeq;

  // Newest `lastSeenMs` heard from each wisp, keyed by wispMac. A lamp
  // re-serves its cached status with the wisp's stamp unchanged, so only a
  // larger stamp is a status emitted since.
  final Map<String, int> _newestStamps = {};

  // The last distinct (wispMac, lastSeenMs) pairs heard, oldest first, each
  // naming one wisp emission in whatever form it arrived. The lamp caches one
  // status at a time, so only a recent emission can come back. Too small a cap
  // lets repeats of a late older copy push out the stamp the lamp still holds.
  final List<(String, int)> _heardStamps = [];
  static const int _heardStampCap = 4;

  /// How many heard stamps the notifier holds, bounded however many it hears.
  @visibleForTesting
  int get heardStampCount => _heardStamps.length;

  /// How long an empty save's status read may take before the save sends.
  @visibleForTesting
  static Duration get saveStatusReadTimeout => _readIdleTimeout;

  // Newest `statusLastSeenMs` on this connection. Every status comes from the
  // one lamp's clock, whichever wisp sent it, so one value orders them all.
  // Cleared on disconnect, since a lamp reboot restarts that clock.
  int? _newestLampStamp;

  // Palette comes from the dedicated `wisppalette` page section, not the
  // status char (whose palette-less NOTIFY would clobber it). A changed
  // `paletteIdPrefix` in a NOTIFY triggers a fresh section read. The editor
  // draft and the saved palette live in manualPaletteDraftProvider, which
  // outlives this notifier.
  bool _manualPaletteWriteFailed = false;

  List<LampColor> get _draft =>
      ref.read(manualPaletteDraftProvider(lampId)).colors;
  List<LampColor> get _savedManualPalette =>
      ref.read(manualPaletteDraftProvider(lampId)).saved;
  void _setDraft(List<LampColor> colors) =>
      ref.read(manualPaletteDraftProvider(lampId).notifier).set(colors);
  void _setSaved(List<LampColor> colors) =>
      ref.read(manualPaletteDraftProvider(lampId).notifier).setSaved(colors);

  bool _paletteKnown = false;
  // Counts each time a known palette turns unknown. A save stops resending
  // once it moves, even if a read has made the palette known again: that
  // read may be another wisp's palette, which a resend would overwrite.
  int _paletteLosses = 0;
  bool get _currentPaletteKnown => _paletteKnown;
  set _currentPaletteKnown(bool known) {
    if (_paletteKnown && !known) _paletteLosses++;
    _paletteKnown = known;
  }

  String _lastPaletteIdPrefix = '';
  bool _paletteRereadInFlight = false;

  // Each wisp heard stating its Manual palette (`manualId`), with the id last
  // stated: null after a reconnect until it states one again. A wisp in here
  // gets the stated-palette rules; any other the prefix rules.
  final Map<String, String?> _manualIds = {};
  // The `manualSeq` each such wisp echoed last.
  final Map<String, int> _manualSeqs = {};
  // The wisp the known palette belongs to, the wisp of the latest present
  // status, and whether the wisp sent that status (the lamp's hello-only
  // merge did not).
  String? _paletteMac;
  String? _statusMac;
  bool _statusFromWisp = false;

  Set<String>? _claimedMacs;
  Map<String, ({LampColor base, LampColor shade})> _livePaint = const {};
  bool _claimsReadInFlight = false;
  DateTime? _lastClaimsReadAt;
  DateTime? _lastStatusPollAt;

  /// True from a shuffle tap until a claims read, started after the wisp
  /// reported a new shuffle seed, shows [_livePaint] changed from the tap, so
  /// the preview can flag the residual mesh-relay lag instead of
  /// silently showing pre-shuffle colors. Bounded by [_recalcFallback] so a
  /// shuffle that produces no visible change (e.g. no claimed lamps) can't
  /// hang the indicator.
  static const Duration _recalcFallback = Duration(seconds: 8);
  bool _colorsRecalculating = false;
  Map<String, ({LampColor base, LampColor shade})>? _shuffleSnapshot;
  Timer? _recalcFallbackTimer;

  // The wisp's shuffle seed when Shuffle was tapped, null when unknown, and
  // whether a status or config has since reported a different one. Until the
  // seed moves the wisp may not have re-rolled, and a drift step alone can
  // change the chips, so no claims read ends [colorsRecalculating] before.
  int? _shuffleSeedAtTap;
  bool _shuffleSeedMoved = false;

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

  /// The last manual palette known saved on the wisp for this lamp, from a
  /// palette read or a confirmed save. Empty until either.
  List<LampColor> get savedManualPalette => _savedManualPalette;

  /// True when the last manual-palette write exhausted its retries without
  /// the wisp echoing it back. Drives the editor's "not saved" indicator;
  /// cleared once a later write confirms or a fresh edit is scheduled.
  bool get manualPaletteWriteFailed => _manualPaletteWriteFailed;

  /// The in-flight editor draft. Mutates as the user adds, edits, reorders
  /// or deletes swatches; flushed to the wisp by [setManualPalette]. Widgets
  /// should watch [manualPaletteDraftProvider] directly to rebuild on edit.
  List<LampColor> get draftManualPalette => _draft;

  /// True when the wisp holds more than [WispRepository.kManualPaletteMaxColors]
  /// colours, so the editor shows only the first ones.
  bool get manualPaletteTrimmed =>
      _savedManualPalette.length > WispRepository.kManualPaletteMaxColors;

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

  // A config read skipped while another read held the link; it runs as soon
  // as the link is free.
  bool _configReadOwed = false;

  // While a split status's wisp has no config held, re-poll and re-read on a
  // backoff. No notify follows when the lamp already held that config, and the
  // next one may be the 30 s heartbeat.
  Timer? _configRefillTimer;

  // Settings writes sent, and how many of them a config read has covered
  // since. The wisp re-sends no config for a write it never applied, so the
  // refill reads it back until the two match.
  int _settingsWrites = 0;
  int _settingsWritesReadBack = 0;
  Duration _configRefillDelay = _configRefillFirst;
  static const Duration _configRefillFirst = Duration(seconds: 1);
  static const Duration _configRefillMax = Duration(seconds: 30);

  /// How long after a settings write the app reads the wisp's config back.
  @visibleForTesting
  static Duration debugConfigReadBackDelay = const Duration(seconds: 5);

  // Every status the wisp sent, config merged, ahead of the source-write
  // guard. The confirm waits listen here so a status that only a section
  // re-read could recover still confirms. Only wisp statuses land here. The
  // held state can be optimistic, and a confirm read off it confirms itself.
  late StreamController<WispStatus> _incoming;

  // Every opSeq the wisp reports, from a status or a `wispconfig` read.
  late StreamController<int> _opSeqs;

  // FbpBleClient has no internal lock, so two concurrent characteristic
  // reads stall the GATT flow on Android; every read here skips while
  // another is in flight. A skipped config read runs once the link is free;
  // the others retry on the next notify.
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
    _manualIds.clear();
    _manualSeqs.clear();
    _paletteMac = null;
    _statusMac = null;
    _statusFromWisp = false;

    _claimedMacs = null;
    _livePaint = const {};
    _claimsReadInFlight = false;
    _lastClaimsReadAt = null;
    _lastStatusPollAt = null;
    _colorsRecalculating = false;
    _shuffleSnapshot = null;
    _shuffleSeedAtTap = null;
    _shuffleSeedMoved = false;

    _newestLampStamp = null;
    _config = null;
    _configMac = null;
    _lastConfigSeenMs = null;
    _configRereadInFlight = false;
    _statusRereadInFlight = false;
    _lastConfigPollAt = null;
    _configReadOwed = false;
    _configRefillTimer = null;
    _configRefillDelay = _configRefillFirst;
    _settingsWrites = 0;
    _settingsWritesReadBack = 0;
    final incoming = _incoming = StreamController<WispStatus>.broadcast();
    final opSeqs = _opSeqs = StreamController<int>.broadcast();

    ref.onDispose(() {
      _disposed = true;
      _sub?.cancel();
      _connSub?.cancel();
      unawaited(incoming.close());
      unawaited(opSeqs.close());
      _offColorWriteTimer?.cancel();
      _driftWriteTimer?.cancel();
      _brightnessWriteTimer?.cancel();
      _recalcFallbackTimer?.cancel();
      _configRefillTimer?.cancel();
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

    // An untouched draft only repeats what an earlier visit read, and the
    // wisp may have changed or emptied its palette since. Start this visit
    // from nothing, as a first visit does, so its own read seeds the editor.
    if (_draftUntouched) {
      ref.read(manualPaletteDraftProvider(lampId).notifier).clear();
    }

    // The lamp is unbonded, so its notify subscription dies with the link
    // and has to be renewed on every reconnect.
    var linkUp = true;
    _connSub = ble.watchConnected(lampId).listen((connected) {
      if (!connected) {
        linkUp = false;
        _newestLampStamp = null;
        return;
      }
      if (linkUp) return;
      linkUp = true;
      _subscribeStatus(ble);
      _restartConfigRefill();
      _forgetStatedPalettes();
    });

    // Subscribe before the initial read to never miss a notify that
    // fires between the read returning and the listener attaching.
    _subscribeStatus(ble);

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
      _noteStamp(initial);
      _notePaletteStatus(initial);
      // Palette rides its own reliable section read, decoupled from the
      // notify-clobbered status char. Awaited before _loadClaims so the two
      // BLE reads don't collide on FbpBleClient's lockless flow.
      await _readManualPaletteSection(initial, bumpState: false);
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
      _scheduleConfigRefill(initial);
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

  void _subscribeStatus(BleClient ble) {
    _sub?.cancel();
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
  }

  /// Read the wisp's manual palette from its dedicated section, decide
  /// whether the palette is known, and seed the editor. The rules, for a wisp
  /// that states its palette (`manualId`) and for one that does not, are in
  /// docs/dev/networking.md (`manualId`, `paletteIdPrefix`).
  /// [WispRepository.readManualPalette] never throws. Serialized against
  /// [_loadClaims] via the in-flight flags, since FbpBleClient has no
  /// internal lock and two concurrent reads stall the flow.
  /// [bumpState] re-emits after ingesting so a notify-triggered re-read paints
  /// the new palette. Off during build: emitting there resolves the provider's
  /// future early, letting an optimistic write land before build's return
  /// clobbers it.
  Future<void> _readManualPaletteSection(
    WispStatus forStatus, {
    bool bumpState = true,
  }) async {
    if (_readBusy || _disposed) return;
    _paletteRereadInFlight = true;
    try {
      final read = await _repo.readManualPalette();
      if (_disposed) return;
      final mac = forStatus.wispMac;
      // A read for a status the wisp did not send may have been served from
      // an empty or stale lamp cache, whatever status lands meanwhile.
      final counts = forStatus.fromWisp && _heardInFull(mac);
      if (_statesPalette(mac) || _statesPalette(_statusMac)) {
        final stated = _manualIds[mac];
        final known = read != null &&
            counts &&
            WispRepository.statusManualId(read) == stated;
        _currentPaletteKnown = known;
        if (known) {
          _paletteMac = mac;
          _ingestManualPalette(read, emptyCounts: true);
        }
      } else {
        final palette = read ?? const <LampColor>[];
        // Outside Manual the prefix names the Osc palette.
        final namesPalette = forStatus.source == WispSourceMode.manual &&
            forStatus.paletteIdPrefix.isNotEmpty;
        _currentPaletteKnown =
            counts && (palette.isNotEmpty || !namesPalette);
        if (_currentPaletteKnown) _paletteMac = mac;
        _ingestManualPalette(palette);
      }
      if (bumpState) _bumpState();
    } finally {
      _paletteRereadInFlight = false;
      _onReadDone();
    }
  }

  /// Seed the editor from a freshly-read palette, the draft's only seed. A
  /// draft that differs from the previous saved palette, an emptied one
  /// included, is the user's edit and stays. An empty read seeds nothing
  /// unless [emptyCounts], when the wisp stated an empty palette.
  void _ingestManualPalette(
    List<LampColor> palette, {
    bool emptyCounts = false,
  }) {
    if (palette.isEmpty && !emptyCounts) return;
    if (_palettesEqual(_savedManualPalette, palette)) return;
    final wasDraftPristine = _draftUntouched;
    _setSaved(palette);
    if (wasDraftPristine) {
      _setDraft(_editorSeed(palette));
    }
  }

  // The draft the editor starts from for [saved]: its first colours, up to
  // the cap.
  static List<LampColor> _editorSeed(List<LampColor> saved) =>
      saved.take(WispRepository.kManualPaletteMaxColors).toList();

  bool get _draftUntouched =>
      _palettesEqual(_draft, _editorSeed(_savedManualPalette));

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
    final seedMoved = _shuffleSeedMoved;
    try {
      final parsed = await _repo.readClaims();
      if (_disposed) return;
      _claimedMacs = parsed?.macs;
      _livePaint = parsed?.colors ?? const {};
      final snapshot = _shuffleSnapshot;
      if (_colorsRecalculating &&
          seedMoved &&
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
      _onReadDone();
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
    _noteStamp(next);
    final paletteWasKnown = _currentPaletteKnown;
    _notePaletteStatus(next);
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
      if (paletteWasKnown && !_currentPaletteKnown) _bumpState();
      return;
    }
    next = guarded;
    _noteShuffleSeed(next);
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
    _scheduleConfigRefill(next);
    debugPrint(
      '[wisp_notifier] notify lamp=$lampId '
      'wispMac=${next.wispMac} present=${next.present} source=${next.source}',
    );
  }

  void _noteStamp(WispStatus status) {
    final lampStamp = status.statusLastSeenMs;
    final newestLamp = _newestLampStamp;
    if (lampStamp != null && (newestLamp == null || lampStamp > newestLamp)) {
      _newestLampStamp = lampStamp;
    }
    final mac = status.wispMac;
    final stamp = status.lastSeenMs;
    if (mac == null || stamp == null) return;
    final heard = (mac, stamp);
    _heardStamps
      ..remove(heard)
      ..add(heard);
    if (_heardStamps.length > _heardStampCap) _heardStamps.removeAt(0);
    final newest = _newestStamps[mac];
    if (newest == null || stamp > newest) _newestStamps[mac] = stamp;
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
      _onReadDone();
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
  // means the section holds something to read. A busy read leaves the stamp
  // unconsumed and is owed once reads go idle.
  void _maybeRereadConfig(WispStatus next) {
    final seen = next.configLastSeenMs;
    if (seen == null || seen == _lastConfigSeenMs) return;
    unawaited(_readConfigSection(next));
  }

  /// Read the `wispconfig` section for the wisp [forStatus] describes and
  /// hold it. With [emit], re-publish the current status with it merged.
  /// Returns the config read, or null when the read was skipped as busy (and
  /// is then owed), failed, or found none.
  Future<WispConfig?> _readConfigSection(
    WispStatus forStatus, {
    bool emit = true,
  }) async {
    if (_disposed) return null;
    if (_readBusy) {
      _configReadOwed = true;
      return null;
    }
    _configRereadInFlight = true;
    final WispConfig? config;
    try {
      config = await _repo.readConfig();
    } finally {
      _configRereadInFlight = false;
      _onReadDone();
    }
    if (_disposed || config == null) return null;
    _config = config;
    _configMac = forStatus.wispMac;
    _lastConfigSeenMs = forStatus.configLastSeenMs;
    if (!emit) return config;
    final cur = state.value;
    if (cur == null) return config;
    final merged = _withHeldConfig(cur);
    _noteShuffleSeed(merged);
    _opSeqs.add(merged.opSeq);
    state = AsyncData(merged);
    return config;
  }

  void _onReadDone() {
    if (!_configReadOwed || _readBusy || _disposed) return;
    final cur = state.value;
    if (cur == null) return;
    _configReadOwed = false;
    unawaited(_readConfigSection(cur));
  }

  bool _configToRead(WispStatus? status) =>
      status != null &&
      status.present &&
      status.configSplit &&
      (!status.configKnown || _settingsWrites != _settingsWritesReadBack);

  void _restartConfigRefill() {
    _configRefillTimer?.cancel();
    _configRefillTimer = null;
    _configRefillDelay = _configRefillFirst;
    final cur = state.value;
    if (cur != null) _scheduleConfigRefill(cur);
  }

  void _scheduleConfigRefill(WispStatus status) {
    if (_disposed) return;
    if (!_configToRead(status)) {
      _configRefillTimer?.cancel();
      _configRefillTimer = null;
      _configRefillDelay = _configRefillFirst;
      return;
    }
    if (_configRefillTimer != null) return;
    _configRefillTimer = Timer(_configRefillDelay, () async {
      _configRefillTimer = null;
      final cur = state.value;
      if (_disposed || !_configToRead(cur)) return;
      final writes = _settingsWrites;
      final next = _configRefillDelay * 2;
      _configRefillDelay = next > _configRefillMax ? _configRefillMax : next;
      // Every build read can land before the auth handshake, and a lamp with
      // no config cached merges no configLastSeenMs, so nothing else marks
      // the sections. A lamp before 1.2.8 never serves them.
      if (!_repo.wispSectionsSupported) await _rereadStatusSection();
      if (_disposed) return;
      if (_repo.wispSectionsSupported) {
        await pollConfig();
        if (_disposed) return;
        final read = await _readConfigSection(state.value ?? cur!);
        if (read != null) _settingsWritesReadBack = writes;
      }
      final after = state.value;
      if (after != null) _scheduleConfigRefill(after);
    });
  }

  // Read the config back once a settings write has had time to land, so a
  // write the wisp never applied stops showing.
  void _readBackAfterWrite() {
    if (_disposed) return;
    _settingsWrites++;
    _configRefillTimer?.cancel();
    _configRefillTimer = null;
    _configRefillDelay = debugConfigReadBackDelay;
    final cur = state.value;
    if (cur != null) _scheduleConfigRefill(cur);
  }

  void _noteShuffleSeed(WispStatus status) {
    final atTap = _shuffleSeedAtTap;
    if (atTap != null && status.configKnown && status.shuffleSeed != atTap) {
      _shuffleSeedMoved = true;
    }
  }

  bool _statesPalette(String? mac) =>
      mac != null && _manualIds.containsKey(mac);

  // Whether the latest present status is one [mac] sent. Until then the
  // lamp's palette cache may be empty or another wisp's.
  bool _heardInFull(String? mac) =>
      mac != null && mac == _statusMac && _statusFromWisp;

  // A present status, before the source-write guard and the read-busy skip:
  // note the palette its wisp states, and return the palette to unknown when
  // it is not the one held from that wisp, when the lamp now relays another
  // wisp, or when the wisp did not send it.
  void _notePaletteStatus(WispStatus status) {
    final mac = status.wispMac;
    if (mac == null) return;
    if (!status.fromWisp) _currentPaletteKnown = false;
    final stated = status.manualId;
    if (stated != null) {
      _manualIds[mac] = stated;
      _manualSeqs[mac] = status.manualSeq;
      if (mac != _paletteMac ||
          stated != WispRepository.statusManualId(_savedManualPalette)) {
        _currentPaletteKnown = false;
      }
    }
    if (_paletteMac != null && _paletteMac != mac) {
      _currentPaletteKnown = false;
    }
    _statusMac = mac;
    _statusFromWisp = status.fromWisp;
  }

  // Another phone may have changed the palette while the link was down, so a
  // wisp that states its palette has to state it again.
  void _forgetStatedPalettes() {
    if (_manualIds.isEmpty) return;
    _manualIds.updateAll((_, _) => null);
    if (_statesPalette(_paletteMac) || _statesPalette(_statusMac)) {
      _currentPaletteKnown = false;
    }
    _bumpState();
  }

  // Start the palette read a status calls for, by the rules in
  // docs/dev/networking.md (`manualId`, `paletteIdPrefix`). A read skipped
  // while another is busy waits for the next status.
  void _maybeRereadForPalette(WispStatus next) {
    if (!next.fromWisp) return;
    final mac = next.wispMac;
    if (_statesPalette(mac)) {
      final stated = next.manualId;
      if (stated == null || _readBusy) return;
      if (!_currentPaletteKnown) unawaited(_readManualPaletteSection(next));
      return;
    }
    final prefix = next.paletteIdPrefix;
    final unread = !_currentPaletteKnown && next.present;
    if (prefix == _lastPaletteIdPrefix && !unread) return;
    // An empty prefix has no palette to read, but recording it lets the same
    // palette coming back register as a change.
    if (prefix.isEmpty && !unread) {
      _lastPaletteIdPrefix = prefix;
      return;
    }
    if (_readBusy) return;
    _lastPaletteIdPrefix = prefix;
    unawaited(_readManualPaletteSection(next));
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
  /// advance arrives and the window elapses. Sized for `opSeq` arriving only
  /// through a config read (status burst, config, notify, section read) with
  /// about two lost frames on the way.
  static const Duration _opConfirmTimeout = Duration(seconds: 10);

  /// Seal [sendOp] under [currentGuess] and wait for the wisp to confirm it
  /// applied by advancing `opSeq`. Restores the previously-cached password on
  /// failure so a wrong guess doesn't strand the repo on a bad secret.
  Future<bool> _sendVerifiedPasswordOp({
    required String currentGuess,
    required Future<void> Function() sendOp,
  }) async {
    // opSeq rides the config: refresh it from the section first, waiting out
    // any read in flight, so the baseline is the wisp's current value. For a
    // split status nothing else carries it, so without a fresh read there is
    // no baseline, and a placeholder 0 or a stale held value would let a
    // later opSeq confirm a rejected change.
    final cur = state.value;
    WispConfig? fresh;
    if (cur != null && _repo.wispSectionsSupported) {
      await _awaitReadsIdle();
      fresh = await _readConfigSection(state.value ?? cur);
    }
    final now = state.value;
    if (now != null && now.configSplit && fresh == null) {
      throw StateError('wisp settings not read; opSeq baseline unknown');
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
    final held = state.value;
    _shuffleSeedAtTap =
        held != null && held.configKnown ? held.shuffleSeed : null;
    _shuffleSeedMoved = _shuffleSeedAtTap == null;
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
  /// reflects in local state until the wisp's settings come back, at the
  /// latest when the app reads them back a few seconds after the write.
  Future<void> setLedStrip(String ledType, int pixelCount) async {
    final cur = state.value ?? WispStatus.empty;
    state = AsyncData(cur.copyWith(ledType: ledType, pixelCount: pixelCount));
    _config = _config?.copyWith(ledType: ledType, pixelCount: pixelCount);
    try {
      await _repo.setLedStrip(ledType, pixelCount);
    } catch (e, st) {
      debugPrint('WispNotifier.setLedStrip write failed: $e\n$st');
    }
    _readBackAfterWrite();
  }

  /// Debounce slider drags to one write per [_driftDebounce] window.
  void setDrift(int intervalMs, int fadePct) {
    _pendingDrift = (intervalMs, fadePct);
    _config = _config?.copyWith(
      driftIntervalMs: intervalMs,
      driftFadePct: fadePct,
    );
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
    _readBackAfterWrite();
  }

  /// Set the space-brightness factor (0..100). Optimistically reflects in
  /// local state so the slider tracks the drag, then debounces the BLE write
  /// at [_brightnessDebounce] to bound NVS wear. The wisp's settings replace
  /// the value once they come back, at the latest when the app reads them
  /// back a few seconds after the write.
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
    _readBackAfterWrite();
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
    // An empty read outside Manual may predate the lamp caching the palette,
    // so Manual shows the loading line until a read for it lands.
    if (mode == WispSourceMode.manual &&
        cur.source != WispSourceMode.manual &&
        _savedManualPalette.isEmpty &&
        !_statesPalette(cur.wispMac)) {
      _currentPaletteKnown = false;
    }
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
  /// resending immediately and returns false. A newer op owns the state or
  /// the op became moot, so a resend from this loop must not reach the wisp.
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
    if (_pendingSourceMode == null) return incoming;
    if (_isStaleSourceEcho(incoming)) return null;
    _pendingSourceMode = null;
    _pendingSourceUntil = null;
    return incoming;
  }

  /// Whether [status] is a pre-write echo the source-write guard drops. That
  /// is a [setSource] pending inside its window and a [status] reporting
  /// another source.
  bool _isStaleSourceEcho(WispStatus status) {
    final pendingMode = _pendingSourceMode;
    final until = _pendingSourceUntil;
    return pendingMode != null &&
        until != null &&
        !DateTime.now().isAfter(until) &&
        status.source != pendingMode;
  }

  // Each helper emits a fresh draft list so the provider's watchers rebuild.
  // Editing is local preview only; the draft reaches the wisp on an explicit
  // [setManualPalette] (Save), never per edit.

  /// Replace the whole draft palette in one shot (the color-stops editor
  /// streams the full list per edit). Keeps the first
  /// [WispRepository.kManualPaletteMaxColors].
  void setManualPaletteDraft(List<LampColor> colors) {
    final capped = _editorSeed(colors);
    if (_palettesEqual(_draft, capped)) return;
    _setDraft(capped);
  }

  /// Append a swatch to the draft. Ignored once the draft holds
  /// [WispRepository.kManualPaletteMaxColors], so the UI's `+` button can be
  /// permissive.
  void appendManualPaletteColor(LampColor color) {
    final draft = _draft;
    if (draft.length >= WispRepository.kManualPaletteMaxColors) return;
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
  /// snapshot. A wisp that states its palette gets a `seq` with the op, the
  /// same on every resend, and confirms on its echo whatever its source mode.
  /// While [paletteLoading] it sends nothing, sets the flag and throws, and a
  /// save in flight sends no further copy once the palette has gone unknown,
  /// even if it is known again by the next resend. A sheet left open
  /// across a display switch or a reconnect would otherwise write over a
  /// palette the app has not read.
  Future<void> setManualPalette() async {
    if (paletteLoading) {
      _manualPaletteWriteFailed = true;
      _bumpState();
      throw StateError('wisp palette not read');
    }
    final losses = _paletteLosses;
    final wispMac = state.value?.wispMac;
    if (_statesPalette(wispMac)) return _saveEchoed(wispMac!, losses);
    final committed = List<LampColor>.from(_draft);
    // The wisp stores an empty palette and clears its palette id, so a later
    // Manual status without paletteIdPrefix confirms it.
    final expectedHash =
        committed.isEmpty ? '' : WispRepository.paletteIdHash(committed);
    final epoch = ++_paletteSaveEpoch;
    final judgeByStamps = committed.isEmpty && wispMac != null;
    if (judgeByStamps) {
      // Take the stamps of the status the lamp holds now, so its own clock
      // gives a baseline even when nothing heard so far carried one.
      await _awaitReadsIdle();
      await _rereadStatusSection()
          .timeout(_readIdleTimeout, onTimeout: () {});
    }
    final emittedAfter = judgeByStamps
        ? (
            lamp: _newestLampStamp,
            wisp: _newestStamps[wispMac],
            heard: {
              for (final (mac, stamp) in _heardStamps)
                if (mac == wispMac) stamp,
            },
          )
        : null;
    var outcome = PaletteConfirmOutcome.notConfirmed;
    final ok = await _sendConfirmed(
      send: () => _repo.setManualPalette(committed),
      confirm: () async {
        outcome = await _confirmPaletteLanded(expectedHash, wispMac,
            emittedAfter: emittedAfter);
        return outcome == PaletteConfirmOutcome.confirmed;
      },
      // Once the wisp left Manual the palette is moot, so stop resending.
      isSuperseded: () =>
          _paletteSaveEpoch != epoch ||
          _paletteLosses != losses ||
          outcome == PaletteConfirmOutcome.sourceChanged,
    );
    if (_disposed || _paletteSaveEpoch != epoch) return;
    _applyPaletteConfirm(committed, ok, wispMac, known: _heardInFull(wispMac));
    if (!ok) {
      if (outcome == PaletteConfirmOutcome.sourceChanged) {
        throw Exception('wisp manual palette write source changed');
      }
      throw Exception('wisp manual palette write not confirmed');
    }
  }

  Future<void> _saveEchoed(String wispMac, int losses) async {
    final committed = List<LampColor>.from(_draft);
    final epoch = ++_paletteSaveEpoch;
    final seq = _mintSaveSeq(wispMac);
    final expected = WispRepository.sentManualId(committed, seq: seq);
    final ok = await _sendConfirmed(
      send: () => _repo.setManualPalette(committed, seq: seq),
      confirm: () => _confirmPaletteEcho(wispMac, seq, expected),
      isSuperseded: () =>
          _paletteSaveEpoch != epoch || _paletteLosses != losses,
    );
    if (_disposed || _paletteSaveEpoch != epoch) return;
    // Before the write's ack, this wisp may have stated another palette, or
    // the lamp may have moved on to another wisp.
    _applyPaletteConfirm(committed, ok, wispMac,
        known: _heardInFull(wispMac) && _manualIds[wispMac] == expected);
    if (!ok) throw Exception('wisp manual palette write not confirmed');
  }

  // Steps from a random start through 1..65535 and skips the seq the wisp
  // echoes now. A status already carrying it would confirm a save the wisp
  // never applied whenever the palette matched.
  int _mintSaveSeq(String wispMac) {
    final last = _lastSaveSeq;
    var seq = last == null ? debugSeqStart() : last % _seqMax + 1;
    if (seq == _manualSeqs[wispMac]) seq = seq % _seqMax + 1;
    _lastSaveSeq = seq;
    return seq;
  }

  /// Resolve true once a status from [wispMac] echoes [seq] with [manualId]
  /// as its palette, emitted after the wisp stored this save, or false when
  /// [_sourceConfirmTimeout] elapses. Its source mode does not matter: the
  /// wisp stores the palette in any mode.
  Future<bool> _confirmPaletteEcho(String wispMac, int seq, String manualId) {
    return _incoming.stream
        .firstWhere((s) =>
            s.wispMac == wispMac &&
            s.manualSeq == seq &&
            s.manualId == manualId)
        .timeout(_sourceConfirmTimeout)
        .then((_) => true)
        .catchError((_) => false);
  }

  void _applyPaletteConfirm(
    List<LampColor> committed,
    bool ok,
    String? wispMac, {
    required bool known,
  }) {
    if (ok) {
      _setSaved(committed);
      _currentPaletteKnown = known;
      _paletteMac = wispMac;
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
  /// prefix. With [emittedAfter], a prefix match also needs [_emittedSince].
  /// Every wispStatus NOTIFY carries the palette id state, so no READ-poll is
  /// needed.
  ///
  /// If that wisp's `source` leaves Manual (or a local [_pendingSourceMode]
  /// already did) the palette write is moot and the outcome is
  /// [PaletteConfirmOutcome.sourceChanged]. A status the source-write guard
  /// drops as stale decides nothing.
  Future<PaletteConfirmOutcome> _confirmPaletteLanded(
    String expectedHash,
    String? wispMac, {
    ({int? lamp, int? wisp, Set<int> heard})? emittedAfter,
  }) {
    final localFlip = _pendingSourceMode != null &&
        _pendingSourceMode != WispSourceMode.manual;
    if (localFlip) return Future.value(PaletteConfirmOutcome.sourceChanged);
    return _incoming.stream
        .firstWhere((s) {
          final emittedSince =
              emittedAfter == null || _emittedSince(s, emittedAfter);
          return s.present &&
              s.wispMac == wispMac &&
              !_isStaleSourceEcho(s) &&
              ((s.paletteIdPrefix == expectedHash && emittedSince) ||
                  s.source != WispSourceMode.manual);
        })
        .timeout(_sourceConfirmTimeout)
        .then((s) => s.source == WispSourceMode.manual
            ? PaletteConfirmOutcome.confirmed
            : PaletteConfirmOutcome.sourceChanged)
        .catchError((_) => PaletteConfirmOutcome.notConfirmed);
  }

  /// Whether [s] is newer than the stamps heard before the save. A
  /// `lastSeenMs` in `after.heard` is an emission heard before it and never
  /// counts. Otherwise a `statusLastSeenMs` above `after.lamp` decides when
  /// both are known, and failing that [s] needs a `lastSeenMs` above
  /// `after.wisp`, unchecked when `after.wisp` is unset.
  static bool _emittedSince(
    WispStatus s,
    ({int? lamp, int? wisp, Set<int> heard}) after,
  ) {
    final stamp = s.lastSeenMs;
    if (stamp != null && after.heard.contains(stamp)) return false;
    final lampStamp = s.statusLastSeenMs;
    final lampAfter = after.lamp;
    if (lampStamp != null && lampAfter != null) return lampStamp > lampAfter;
    final wispAfter = after.wisp;
    return wispAfter == null || (stamp != null && stamp > wispAfter);
  }

  /// Every emit reaches listeners, an equal status included. The screen reads
  /// state held beside [state] (palette known, write failed, claims, live
  /// paint), so an unchanged status can still carry a change.
  @override
  bool updateShouldNotify(
    AsyncValue<WispStatus> previous,
    AsyncValue<WispStatus> next,
  ) =>
      true;

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
