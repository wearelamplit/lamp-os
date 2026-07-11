import 'dart:async';
import 'dart:convert';
import 'dart:io' show Platform;
import 'dart:typed_data';

import 'package:flutter/widgets.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/lamp_crypto.dart';
import '../../../core/ble/uuids.dart';
import '../../../core/ble/write_coalescer.dart';
import '../../../core/lifecycle/app_lifecycle.dart';
import '../../inventory/application/inventory_notifier.dart';
import 'advanced_session.dart';
import '../../lamp_shell/domain/expression_catalog.dart';
import '../domain/lamp_color.dart';
import '../domain/sections.dart';
import '../../social/domain/social_mode.dart';
import 'auth_client.dart';
import 'control_state.dart';
import 'lamp_auth_required_exception.dart';
import 'lamp_save_status.dart';

part 'control_notifier.g.dart';

/// Per-surface enum for [ControlNotifier.setEditSession]. Bit values
/// match the firmware-side CHAR_EDIT_SESSION mask:
///   - base = 0x01
///   - shade = 0x02
///   - brightness = 0x04
enum EditSurface {
  base(0x01),
  shade(0x02),
  brightness(0x04);

  const EditSurface(this.bit);
  final int bit;
}

/// Marker error type set by ControlNotifier.factoryReset so callers can
/// distinguish "user reset this lamp" from genuine BLE / parse failures.
class FactoryResetSentinel implements Exception {
  const FactoryResetSentinel();
  @override
  String toString() => 'lamp was factory-reset';
}

/// Disables Riverpod's framework-level auto-retry for the control notifier.
/// We run our own reconnect loop with explicit backoff (`_reconnectDelays`)
/// and two concurrent framework retries observed racing the scheduler when
/// multiple lamps failed connect at once. Returning null keeps the provider
/// in error state until an explicit `ref.invalidate` or a new build().
Duration? _noRetry(int retryCount, Object error) => null;

@Riverpod(keepAlive: false, name: 'controlNotifierProvider', retry: _noRetry)
class ControlNotifier extends _$ControlNotifier {
  // Nullable for defensive re-assignability. With framework retry disabled
  // (see `_noRetry`) build() is no longer re-entered on the same instance
  // after a connect/auth failure, but an explicit `ref.invalidate` could
  // still trigger rebuild; nullable shape costs nothing and removes the
  // LateInitializationError class of bug entirely.
  WriteCoalescer? _brightnessWriter;
  WriteCoalescer? _shadeColorsWriter;
  WriteCoalescer? _baseColorsWriter;
  KeyedWriteCoalescer<EditSurface>? _editSessionWriter;
  WriteCoalescer? _zonePreviewWriter;

  // Per-pixel knockout debounce: keyed by pixel index.
  final Map<int, Timer?> _knockoutTimers = {};
  final Map<int, int> _knockoutPending = {};

  StreamSubscription<bool>? _connSub;
  /// CHAR_STATE_NOTIFY subscription. The firmware notifies on transitions
  /// of the active-test set (empty ↔ non-empty) so the expression editor's
  /// Test button can morph without an app-side timer. Payload is a small
  /// JSON object: `{"previewActive": true|false}`. Older firmware emits
  /// `{}` and the parser leaves `previewActive` at its default `false`.
  StreamSubscription<Uint8List>? _stateNotifySub;
  Timer? _reconnectTimer;
  /// Periodic liveness probe. fbp's connectionState stream doesn't reliably
  /// emit the `false` edge when the lamp terminates the link itself — e.g.
  /// it kicks the GATT client when a mesh OTA starts (ble_control
  /// disconnectGattClientsForOta). Without this we'd sit on a zombie
  /// "connected" state until the user's next write fails. _probeLink forces
  /// a real GATT round-trip; a dead link throws and routes to the reconnect
  /// ladder, same as the foreground-resume probe.
  Timer? _probeTimer;
  // ponytail: 3s is the detection-latency vs battery/radio knob; tune on hardware.
  static const _probeInterval = Duration(seconds: 3);
  /// Single-slot guard against concurrent reconnect attempts. Set true at
  /// the top of `_tryReconnect`, cleared in `finally`. Lets us safely
  /// kick `_tryReconnect` from BOTH the scheduled timer (`_scheduleReconnect`)
  /// AND the watchConnected→true edge (`_onConnectionChange`) without
  /// stacking ble.connect + auth + canary calls on top of each other.
  bool _reconnectInFlight = false;
  static const _reconnectDelays = [500, 1000, 2000, 4000, 8000]; // ms, capped
  /// Max consecutive reconnect attempts before giving up and surfacing an
  /// error state instead of polling forever.
  /// 30 attempts × the capped 8 s delay = ~4 minutes of background
  /// reconnect work. Past that, the lamp is almost certainly out of
  /// range and continuing to retry just keeps the radio warm + drains
  /// the user's battery. User can pull to refresh / re-tap the lamp to
  /// reset the loop.
  static const _maxReconnectAttempts = 30;

  // Inventory "last-seen color" debouncer. We don't need to persist every
  // slider tick — only the trailing value. The previous code was awaiting a
  // SharedPreferences write + an inventory-state notification per tick,
  // which dominated frame time during a drag. We collapse all the writes
  // inside a window into one disk write at the trailing edge.
  Timer? _seenFlushTimer;
  LampColor? _pendingSeenShade;
  LampColor? _pendingSeenBase;
  static const _seenFlushDelay = Duration(milliseconds: 500);

  // Commit debouncer. Scheduled after user fences (slider release, picker
  // accept). Trailing-edge: each new call cancels the previous timer so
  // rapid calls collapse to one BLE write. Flushed synchronously on
  // notifier dispose and on AppLifecycleState.paused.
  /// Debounce window after the last user fence (slider release, picker
  /// accept) before commit fires. 500ms matches the spec — feels instant
  /// after release, generous enough that incremental taps collapse to
  /// one commit.
  static const Duration _commitDebounce = Duration(milliseconds: 500);

  /// Live-preview throttle window. WriteCoalescer fires on a timer (not on
  /// the previous write's ACK) so this value paces the BLE writes the user
  /// sees during a continuous gesture. 60ms (~17 Hz per channel) keeps the
  /// lamp visibly tracking under TIGHT or WIDE conn parameters without
  /// saturating fbp's per-device FIFO under sustained drag.
  static const Duration _writeDebounce = Duration(milliseconds: 60);
  Timer? _commitDebounceTimer;
  bool _commitPending = false;


  // Captured from the build argument. Not `late final` because a Riverpod
  // retry re-runs build() on the same notifier instance — the second pass
  // would reassign and a final field throws LateInitializationError.
  late String _deviceId;

  // Cached provider references captured in build(). Timer / writer
  // callbacks fire asynchronously and could land AFTER ref.onDispose
  // has run; calling `ref.read(...)` then throws. The underlying
  // BleClient + InventoryNotifier instances both live in keepAlive
  // providers, so a cached pointer stays valid for the lifetime of
  // those providers (the whole app) regardless of this notifier's
  // own dispose timing.
  late BleClient _ble;
  late InventoryNotifier _inv;

  // ---------------------------------------------------------------------------
  // Inventory color-cache helpers
  // ---------------------------------------------------------------------------

  /// RGBW list shape persisted in inventory (and read back by
  /// `resolveLampColors`). Includes the warm-white byte so warm-heavy
  /// edits render correctly on the My Lamps / picker tiles — the BLE
  /// adv carries only RGB triplets, so the cache is the only source
  /// of W for offline / between-edit rendering.
  List<int> _rgbwList(LampColor c) => [c.r, c.g, c.b, c.w];

  /// Wait for the lamp to reboot and reconnect after a settingsBlob /
  /// password write, then reauthenticate, re-read every section, and
  /// update [state]. Calls [postReload] after the new state has landed.
  ///
  /// Polls with 1s spacing, up to [maxAttempts] (~12s total wait).
  /// Each attempt: `ble.connect` → `authenticate(password)` →
  /// `_readSections`. Transient disconnect-like errors retry. Non-
  /// transient errors and final-attempt failures surface as `AsyncError`.
  Future<void> _awaitReconnectAndReload({
    required BleClient ble,
    required String password,
    required Future<void> Function(ControlState fresh) postReload,
    int maxAttempts = 12,
  }) async {
    // Initial grace: give the lamp a head start on the reboot so the
    // first attempt isn't guaranteed to race the disconnect. ~2s lets
    // the firmware fade-out complete and the actual reboot kick in.
    await Future<void>.delayed(const Duration(seconds: 2));

    ControlState? fresh;
    Object? lastError;
    StackTrace? lastStack;
    for (var attempt = 0; attempt < maxAttempts; attempt++) {
      if (!ref.mounted) return;
      try {
        await ble.connect(_deviceId);
        await AuthClient(ble: ble).authenticate(
            deviceId: _deviceId, password: password);
        fresh = await _readSections(ble);
        break;
      } catch (e, st) {
        lastError = e;
        lastStack = st;
        // Disconnect / not-connected go through the typed path now
        // (BleDisconnectedException from FbpBleClient). The remaining
        // transient signals — discoverServices flakes and connect/auth
        // timeouts — are still detected by message inspection because
        // fbp doesn't surface them as discrete types.
        final msg = e.toString().toLowerCase();
        final isTransient = isBleDisconnectError(e) ||
            msg.contains('discoverservices') ||
            msg.contains('timeout');
        if (!isTransient) break;
        await Future<void>.delayed(const Duration(seconds: 1));
      }
    }

    if (!ref.mounted) return;
    if (fresh == null) {
      state = AsyncError(
          lastError ?? Exception('lamp did not reconnect after save'),
          lastStack ?? StackTrace.current);
      ref.read(lampSaveStatusProvider(_deviceId).notifier).stop();
      return;
    }

    state = AsyncData(fresh);
    try {
      await postReload(fresh);
    } catch (e, st) {
      // postReload is best-effort housekeeping.
      if (ref.mounted) state = AsyncError(e, st);
    }
    ref.read(lampSaveStatusProvider(_deviceId).notifier).stop();
  }

  Future<void> _updateSeen({
    LampColor? shade,
    LampColor? base,
  }) async {
    // Use cached notifier ref so the trailing-flush path
    // (`_seenFlushTimer` + the `ref.onDispose` final flush) can't
    // touch a disposed Riverpod ref.
    await _inv.updateSeen(
      _deviceId,
      shade: shade == null ? null : _rgbwList(shade),
      base: base == null ? null : _rgbwList(base),
    );
  }

  /// Coalesces "last-seen color" updates into one trailing disk write.
  /// Used by `setShadeColor` and `setBaseColors` so a continuous slider
  /// drag at 60 fps doesn't translate into 60 SharedPreferences writes
  /// (and 60 inventory-state notifications that rebuild LampShell).
  void _queueSeen({LampColor? shade, LampColor? base}) {
    if (shade != null) _pendingSeenShade = shade;
    if (base != null) _pendingSeenBase = base;
    _seenFlushTimer?.cancel();
    _seenFlushTimer = Timer(_seenFlushDelay, () => unawaited(_flushSeen()));
  }

  Future<void> _flushSeen() async {
    final s = _pendingSeenShade;
    final b = _pendingSeenBase;
    _pendingSeenShade = null;
    _pendingSeenBase = null;
    if (s == null && b == null) return;
    await _updateSeen(shade: s, base: b);
  }

  @override
  Future<ControlState> build(String deviceId) async {
    _deviceId = deviceId;

    // Cache provider refs once. Used by Timer / coalescer callbacks
    // that can fire after this notifier's onDispose has run.
    _ble = ref.read(bleClientProvider);
    _inv = ref.read(inventoryNotifierProvider.notifier);
    final ble = _ble;
    final inv = await ref.read(inventoryNotifierProvider.future);
    final lamp = inv.firstWhere(
      (l) => l.id == deviceId,
      orElse: () => throw StateError('lamp $deviceId not in inventory'),
    );

    // Skip the cold GATT connect when a pre-warm has already established
    // the link (BleClient.prewarm wired into the BLE adv stream — see
    // nearby_lamps_notifier.dart). Saves the 200-500 ms `device.connect`
    // handshake + 100-400 ms `discoverServices` round-trip on the
    // common "user opens app, taps a nearby paired lamp" path. Cold-tap
    // path (lamp out of range until just before the tap) still pays
    // those costs normally.
    if (!ble.isConnected(deviceId)) {
      try {
        await ble.connect(deviceId);
      } catch (e) {
        // ESP32 BLE+WiFi coexistence intermittently drops the
        // LL_CONNECT_IND handshake, surfacing as Android
        // HCI_ERR_CONN_FAILED_ESTABLISHMENT (status=133). One quiet
        // retry with a short backoff smooths over the common transient
        // case so the user doesn't have to manually retry from the
        // error page on a routine tap.
        if (e is LampAuthRequiredException) rethrow;
        await Future<void>.delayed(const Duration(milliseconds: 500));
        await ble.connect(deviceId);
      }
    }
    // Register disconnect now — only after a successful connect. If connect
    // itself threw, there's nothing to tear down, and registering this
    // earlier would crash dispose on platforms where the BLE client can't
    // run (e.g. flutter_blue_plus in unit tests). If auth or section reads
    // throw below, this still fires on container teardown.
    //
    // The commit flush is bundled here (before the ble.disconnect call)
    // so that _flushPendingCommit fires while the link is still open.
    // ref.onDispose callbacks are called FIFO; if flush were registered
    // later it would race with the disconnect.
    ref.onDispose(() {
      _flushPendingCommit();
      ble.disconnect(deviceId);
    });
    await AuthClient(ble: ble)
        .authenticate(deviceId: deviceId, password: lamp.controlPassword);

    // Auth-gate canary. Firmware returns empty bytes from lampSection on
    // unauthenticated reads so an unauth'd peer can't exfiltrate the
    // password embedded in the section blob. Empty bytes here mean our
    // stored credential is missing or stale → typed sentinel the UI
    // catches and converts into a password prompt.
    final canaryBytes = await ble.readSection(deviceId, 'lamp');
    if (canaryBytes.isEmpty) {
      throw const LampAuthRequiredException();
    }

    final fresh = await _readSections(ble);

    // Heal password divergence: lamp's NVS pw got wiped (re-flash, factory
    // reset) but our cached controlPassword is still non-empty. The lamp
    // is in open-access mode (isAuthed=true unconditionally), so reads
    // and most writes work — but settings_blob still gets encrypted by
    // the app, then dropped by the lamp's decryptOp at password.empty().
    // Authoritative signal is fresh.lamp.hasPassword==false from the
    // section we just read. Clear the cache so future writeSettingsBlob
    // calls hit the pw.isEmpty plaintext branch.
    if (fresh.lamp.hasPassword == false &&
        (lamp.controlPassword?.isNotEmpty ?? false)) {
      await ref
          .read(inventoryNotifierProvider.notifier)
          .updatePassword(deviceId, null);
    }

    // Pipe the lamp's variant identity from the LampSection read into
    // inventory so the OTA flow can fetch the matching per-variant
    // firmware binary even when the lamp is offline.
    final lampType = fresh.lamp.lampType;
    if (lampType != null && lampType.isNotEmpty) {
      await _inv.updateLampType(deviceId, lampType);
    }
    // Mirror fwVersion + fwChannel onto inventory so My Lamps can render
    // each tile's firmware identity offline (and CachedFirmwareNotifier
    // can decide which variants need a fresh fetch).
    if (fresh.lamp.fwVersion != null || fresh.lamp.fwChannel != null) {
      await _inv.updateFirmwareInfo(
        deviceId,
        fwVersion: fresh.lamp.fwVersion,
        fwChannel: fresh.lamp.fwChannel,
      );
    }

    // Live-preview writes are fire-and-forget. Swallow errors here so a
    // pending debounce timer that fires after the lamp disconnects (e.g.
    // post-save reboot, or back-navigation tearing down the provider while
    // a timer is in flight) doesn't crash the app. The next reconnect
    // reloads state from the lamp anyway.
    Future<void> safeWrite(String charUuid, Uint8List v) async {
      try {
        // withoutResponse: true — slider-rate live preview, no need
        // for per-write GATT ACK round-trip.
        await ble.write(
          deviceId,
          BleUuids.controlService,
          charUuid,
          v,
          withoutResponse: true,
        );
      } catch (e) {
        // A live-preview write throwing a disconnect-shaped error is the
        // canonical "link has zombified" signal — fbp's connectionState
        // stream sometimes misses the false edge (backgrounded socket
        // teardown, gatts_if slot leak). Treat it as the disconnect we
        // never observed and kick the reconnect ladder so the user
        // doesn't have to force-stop the app to recover.
        if (isBleDisconnectError(e) && ref.mounted) {
          // Notifier dispose during a live-preview write happens routinely
          // (lamp switch, back-nav, post-save reboot) — `safeWrite`'s
          // own onWrite closure outlives ref. Guard against mutating
          // state on a disposed notifier.
          _onConnectionChange(false);
        }
        // Other exception types (e.g. encryption-required surfaced mid-
        // session, or a transient write rejection) are still dropped —
        // a live-preview write tearing the UI down on an isolated failure
        // would be a worse outcome than the missed paint.
      }
    }

    // Dispose any prior writers (from an earlier build() invocation on this
    // instance after a Riverpod retry) before swapping new ones in.
    _brightnessWriter?.dispose();
    _shadeColorsWriter?.dispose();
    _baseColorsWriter?.dispose();
    _editSessionWriter?.dispose();
    _brightnessWriter = WriteCoalescer(
      onWrite: (v) => safeWrite(BleUuids.brightness, v),
      debounce: _writeDebounce,
    );
    _shadeColorsWriter = WriteCoalescer(
      onWrite: (v) => safeWrite(BleUuids.shadeColors, v),
      debounce: _writeDebounce,
    );
    _baseColorsWriter = WriteCoalescer(
      onWrite: (v) => safeWrite(BleUuids.baseColors, v),
      debounce: _writeDebounce,
    );
    _editSessionWriter = KeyedWriteCoalescer<EditSurface>(
      onWrite: (_, payload) => safeWrite(BleUuids.editSession, payload),
      debounce: _writeDebounce,
    );
    _zonePreviewWriter?.dispose();
    _zonePreviewWriter = WriteCoalescer(
      onWrite: (v) => safeWrite(BleUuids.expressionTest, v),
      debounce: _writeDebounce,
    );

    ref.onDispose(() {
      _brightnessWriter?.dispose();
      _shadeColorsWriter?.dispose();
      _baseColorsWriter?.dispose();
      _editSessionWriter?.dispose();
      _zonePreviewWriter?.dispose();
      for (final t in _knockoutTimers.values) {
        t?.cancel();
      }
      _knockoutTimers.clear();
      _knockoutPending.clear();
      // Flush any pending "last-seen color" to disk before tearing down so
      // the trailing value of an in-flight drag isn't dropped.
      if (_seenFlushTimer?.isActive == true) {
        _seenFlushTimer!.cancel();
        unawaited(_flushSeen());
      }
      // Cancel the debounce timer — the commit flush itself is handled in
      // the earlier onDispose (bundled with disconnect) so it fires while
      // the BLE link is still open. Cancelling here just stops a dangling
      // timer from firing after disconnect.
      _commitDebounceTimer?.cancel();
      // disconnect is handled by the earlier onDispose registered right after
      // connect(), ensuring it always runs even if build() throws mid-way.
    });

    // Subscribe to the connection stream so we can surface unsolicited
    // disconnects and drive the reconnect loop. The first emission will be
    // `true` (we just connected); _onConnectionChange(true) when already
    // connected is a no-op.
    _connSub = ble.watchConnected(deviceId).listen(_onConnectionChange);
    // Catch lamp-side link terminations fbp misses (mesh-OTA kick, etc.).
    // Self-gates: _probeLink no-ops while disconnected/reconnecting.
    // ponytail: skip under `flutter test` — there's no platform BLE and a
    // free-running periodic timer trips the binding's pending-timer check.
    if (!Platform.environment.containsKey('FLUTTER_TEST')) {
      _probeTimer = Timer.periodic(_probeInterval, (_) => _probeLink());
    }
    ref.onDispose(() {
      _connSub?.cancel();
      _reconnectTimer?.cancel();
      _probeTimer?.cancel();
    });

    // Watch CHAR_STATE_NOTIFY. Used today only for the previewActive bit
    // (expression editor Test button morph + auto-reset). Other state-
    // change clients re-fetch via the page protocol, same as before.
    _stateNotifySub = ble
        .subscribe(deviceId, BleUuids.controlService, BleUuids.stateNotify)
        .listen(_onStateNotify);
    ref.onDispose(() => _stateNotifySub?.cancel());

    // Probe the BLE link whenever the app comes back to the foreground.
    // Backgrounded apps can have their GATT connection torn down by the
    // OS (Android process priority, iOS suspend); fbp's connectionState
    // stream doesn't always emit the `false` edge in those cases, so a
    // bare _onConnectionChange listener stays stuck on `true` and any
    // user interaction silently fails. The probe forces a real GATT
    // round-trip — if the link is dead, the read throws
    // BleDisconnectedException and we kick the reconnect ladder.
    ref.listen<AppLifecycleState>(appLifecycleStateProvider, (prev, next) {
      if (next == AppLifecycleState.resumed && prev != next) {
        _probeLink();
      }
      if (next == AppLifecycleState.paused) {
        _flushPendingCommit();
      }
    });

    await _updateSeen(
      shade: fresh.shade.colors.isEmpty
          ? LampColor.black
          : fresh.shade.colors.first,
      base: fresh.base.colors[fresh.base.ac],
    );

    // Mirror the lamp's current display name into the inventory cache so
    // the lamp picker stays in sync with renames that happened on another
    // phone, or that survived a factory-reset + re-adopt. The control
    // screen's "Hello my name is:" header reads live state and is always
    // correct; the picker reads InventoryLamp.name and would otherwise
    // stay frozen at adopt-time.
    await _inv.updateName(deviceId, fresh.lamp.name);

    // Self-heal: if a previous session left the firmware's configurator
    // behaviors stuck in disabled=true (the test_expression / complete
    // protocol can leak that state — see ExpressionEditorScreen.dispose),
    // re-enable them on every connect. Cheap one-shot write; safe no-op
    // when the configurators are already enabled.
    await _completeExpressionTest(ble);

    return fresh;
  }

  /// Reads every section via the page protocol and returns a fresh
  /// ControlState. Assumes the BLE link is connected and authenticated.
  Future<ControlState> _readSections(BleClient ble) async {
    Future<Map<String, dynamic>> readJsonSection(String name) async {
      final bytes = await ble.readSection(_deviceId, name);
      return jsonDecode(utf8.decode(bytes)) as Map<String, dynamic>;
    }

    Future<List<dynamic>> readJsonListSection(String name) async {
      final bytes = await ble.readSection(_deviceId, name);
      return jsonDecode(utf8.decode(bytes)) as List<dynamic>;
    }

    final lampJson = await readJsonSection('lamp');
    final baseJson = await readJsonSection('base');
    final shadeJson = await readJsonSection('shade');
    final homeJson = await readJsonSection('home');
    final exprList = await readJsonListSection('expr');
    return ControlState(
      lamp: LampSection.fromJson(lampJson),
      base: BaseSection.fromJson(baseJson),
      shade: ShadeSection.fromJson(shadeJson),
      home: HomeSection.fromJson(homeJson),
      expressions: ExpressionsSection.fromJson(exprList),
      catalog: await _readCatalog(ble),
    );
  }

  /// Reads the firmware-declared expression catalog. Returns null on older
  /// firmware that doesn't serve the section (empty bytes) or on a malformed
  /// payload, so the editor degrades instead of crashing.
  Future<ExpressionCatalog?> _readCatalog(BleClient ble) async {
    try {
      final bytes = await ble.readSection(_deviceId, 'exprcat');
      if (bytes.isEmpty) return null;
      return ExpressionCatalog.fromJson(
          jsonDecode(utf8.decode(bytes)) as Map<String, dynamic>);
    } catch (_) {
      return null;
    }
  }


  /// Commit fence. Writes a single byte to CHAR_COMMIT — the firmware
  /// persists current RAM state to NVS. The whole fleet exposes
  /// CHAR_COMMIT now (older firmware was retired); this is the only
  /// persistence path.
  Future<void> commit() async {
    final ble = ref.read(bleClientProvider);
    try {
      // WRITE_NR so the commit doesn't stall fbp's per-device FIFO on the
      // GATT ACK round-trip (which under sustained rapid drag-release
      // cycles backs up brightness WRITE_NR writes behind it for seconds).
      await ble.write(
        _deviceId,
        BleUuids.controlService,
        BleUuids.commit,
        Uint8List.fromList([0x01]),
        withoutResponse: true,
      );
    } catch (e, st) {
      debugPrint('controlNotifier.commit failed: $e\n$st');
      rethrow;
    }
  }

  /// Optimistic-update wrapper. Captures the current state, applies
  /// the [transform] immediately so the UI feels instant, then awaits
  /// the [commit] callback that does the BLE write(s). On exception,
  /// restores the captured state and rethrows.
  ///
  /// Per-pane mutators wrap their work in this helper to inherit
  /// snackbar-friendly failure semantics without each call site
  /// duplicating the try/catch dance.
  Future<void> _mutate(
    ControlState Function(ControlState) transform,
    Future<void> Function() commit,
  ) async {
    final prev = state.value;
    if (prev == null) return;
    state = AsyncData(transform(prev));
    try {
      await commit();
    } catch (e) {
      state = AsyncData(prev);
      rethrow;
    }
  }

  /// Test-only access to [_mutate] so unit tests can exercise the
  /// revert path without going through a public mutator.
  @visibleForTesting
  Future<void> mutateForTest(
    ControlState Function(ControlState) transform,
    Future<void> Function() commit,
  ) =>
      _mutate(transform, commit);

  /// Schedule a debounced commit. If called again before the window
  /// expires, the timer is cancelled and rescheduled (trailing-edge).
  void _scheduleCommitDebounced() {
    _commitDebounceTimer?.cancel();
    _commitPending = true;
    _commitDebounceTimer = Timer(_commitDebounce, () async {
      _commitDebounceTimer = null;
      if (!_commitPending) return;
      _commitPending = false;
      try {
        await commit();
      } catch (e, st) {
        debugPrint('controlNotifier._scheduleCommitDebounced failed: $e\n$st');
      }
    });
  }

  /// Synchronously force-flush a pending debounced commit. Called from
  /// dispose + AppLifecycleState.paused so a quick edit-then-leave
  /// doesn't lose the user's last change.
  ///
  /// IMPORTANT: This method MUST NOT call `ref.read(...)` or access any
  /// Riverpod notifier `.state` — it runs from `ref.onDispose` where
  /// Riverpod forbids ref access. Uses cached `_ble` instead.
  void _flushPendingCommit() {
    if (!_commitPending) return;
    _commitDebounceTimer?.cancel();
    _commitDebounceTimer = null;
    _commitPending = false;

    unawaited(_ble.write(
      _deviceId,
      BleUuids.controlService,
      BleUuids.commit,
      Uint8List.fromList([0x01]),
    ).catchError((e, st) {
      debugPrint('controlNotifier._flushPendingCommit failed: $e\n$st');
    }));
  }

  @visibleForTesting
  void scheduleCommitDebouncedForTest() => _scheduleCommitDebounced();

  /// Writes an arbitrary settings_blob JSON map to the lamp.
  ///
  /// The `reboot` flag is merged into the map before encryption.
  /// Firmware that honors the flag skips its reboot cycle when
  /// `reboot == false`; older firmware ignores the unknown key and
  /// always reboots.
  ///
  /// Use `reboot: false` for discrete settings mutators (rename,
  /// personality, home toggle) where only CHAR_COMMIT follows.
  /// Use `reboot: true` (the default) for Advanced LED changes and
  /// factory-reset-adjacent writes that must trigger a full reboot.
  ///
  /// When `reboot == true` the expected BleDisconnectedException
  /// (firmware drops the link during its reboot cycle) is swallowed.
  /// When `reboot == false` a disconnect is a real error and is
  /// rethrown so the caller can surface it.
  ///
  /// Throws on BLE write failure (caller wraps + snackbars).
  Future<void> writeSettingsBlob(
    Map<String, dynamic> blob, {
    bool reboot = true,
  }) async {
    final ble = ref.read(bleClientProvider);
    final inv = await ref.read(inventoryNotifierProvider.future);
    final lamp = inv.firstWhere(
      (l) => l.id == _deviceId,
      orElse: () => throw StateError('lamp $_deviceId not in inventory'),
    );
    final pw = lamp.controlPassword ?? '';

    final payloadBlob = <String, dynamic>{
      ...blob,
      'reboot': reboot,
    };
    final blobJson = jsonEncode(payloadBlob);

    final payload = pw.isEmpty
        ? Uint8List.fromList([
            LampCrypto.magicPlaintext,
            ...utf8.encode(blobJson),
          ])
        : await LampCrypto.encryptOp(
            op: payloadBlob,
            password: pw,
            saltUuid16: uuidSaltLE16(BleUuids.settingsBlob),
            charShortName: 'settingsBlob',
          );

    try {
      await ble.write(
        _deviceId,
        BleUuids.controlService,
        BleUuids.settingsBlob,
        payload,
        allowLongWrite: true,
      );
    } on BleDisconnectedException {
      // Expected when reboot==true — firmware drops the link mid-write.
      // For reboot==false this is a real disconnect: rethrow so the
      // caller can surface it.
      if (!reboot) rethrow;
    } catch (e) {
      if (!isBleDisconnectError(e)) rethrow;
      // Generic disconnect wrapper (non-typed path): same semantics.
      if (!reboot) rethrow;
    }
  }

  /// Signal to the lamp that the operator is actively editing colours
  /// (or brightness) for [surface]. The lamp uses this to drop
  /// wisp-sourced overrides on that surface for the duration of the
  /// session, so a wisp paint frame mid-drag doesn't fight the user's
  /// pick. Always pair an `open=true` call with a matching `open=false`
  /// when the picker / drag closes (use `try/finally`). The lamp's
  /// own onDisconnect handler also clears every flag as a defensive
  /// sweep, so a forgotten close (app crash, force-stop) recovers on
  /// the next reconnect.
  ///
  /// Routed through a per-surface KeyedWriteCoalescer so a flurry of
  /// open/close pairs from rapid micro-drags collapses to the latest
  /// state per surface — drag-tap-drag-tap on the brightness slider
  /// becomes one open + one close, not a queue of N pairs.
  void setEditSession(EditSurface surface, bool open) {
    _editSessionWriter?.schedule(
      surface,
      Uint8List.fromList([surface.bit, open ? 1 : 0]),
    );
  }

  Future<void> setBrightness(int value) async {
    final v = value.clamp(0, 100);
    final cur = state.value;
    if (cur == null) return;
    state = AsyncData(cur.copyWith(
      lamp: LampSection(
        name: cur.lamp.name,
        brightness: v,
        advancedEnabled: cur.lamp.advancedEnabled,
        webappEnabled: cur.lamp.webappEnabled,
        socialMode: cur.lamp.socialMode,
        fwVersion: cur.lamp.fwVersion,
        fwChannel: cur.lamp.fwChannel,
      ),
    ));
    _brightnessWriter?.schedule(Uint8List.fromList([v]));
  }

  /// Called from the brightness slider's `onChangeEnd` to schedule a
  /// debounced commit.
  void scheduleBrightnessCommit() {
    _scheduleCommitDebounced();
  }

  /// Synchronous force-flush exposed for the knockout screen's
  /// PopScope hook. The debounce timer may not have fired yet and the
  /// notifier's onDispose may not fire at route-pop if the notifier
  /// is app-scoped.
  void flushKnockoutCommit() {
    _flushPendingCommit();
  }

  Future<void> setShadeColor(LampColor color) async {
    // Single-color convenience: wraps the color in a 1-element list and
    // routes through `setShadeColors`. Existing callers (expression
    // editor live-preview, lamp_preview thumbnails) keep their old
    // signature.
    return setShadeColors([color]);
  }

  Future<void> setShadeColors(List<LampColor> colors) async {
    final cur = state.value;
    if (cur == null) return;
    // Sync broadcast change into segments[0] so per-segment viewers stay current.
    final segs = cur.shade.segments.isEmpty
        ? <Segment>[]
        : [
            Segment(
              name: cur.shade.segments.first.name,
              px: cur.shade.segments.first.px,
              colors: colors,
            ),
            ...cur.shade.segments.skip(1),
          ];
    state = AsyncData(cur.copyWith(
      shade: ShadeSection(
        px: cur.shade.px,
        bpp: cur.shade.bpp,
        byteOrder: cur.shade.byteOrder,
        colors: colors,
        colorsEditable: cur.shade.colorsEditable,
        segments: segs,
      ),
    ));
    _shadeColorsWriter?.schedule(_encodeColors(colors));
    // Inventory "last seen" cache mirrors the first stop — same shape as
    // the pre-gradient single-color path, so the lamp picker's swatch
    // preview stays representative.
    if (colors.isNotEmpty) {
      _queueSeen(shade: colors.first);
    }
  }

  /// Live-update a specific shade segment's colors. Segment 0 routes through
  /// setShadeColors (broadcast path); segments > 0 stream over CHAR_SHADE_COLORS
  /// as {"seg":k,"colors":[...]}. Persisted by the Save-triggered CHAR_COMMIT,
  /// same path as segment 0.
  Future<void> setShadeSegmentColors(int segIdx, List<LampColor> colors) async {
    if (segIdx == 0) {
      await setShadeColors(colors);
      return;
    }
    final cur = state.value;
    if (cur == null) return;
    final segs = List<Segment>.from(cur.shade.segments);
    if (segIdx >= segs.length) return;
    segs[segIdx] = Segment(name: segs[segIdx].name, px: segs[segIdx].px, colors: colors);
    state = AsyncData(cur.copyWith(
      shade: ShadeSection(
        px: cur.shade.px,
        bpp: cur.shade.bpp,
        byteOrder: cur.shade.byteOrder,
        colors: cur.shade.colors,
        colorsEditable: cur.shade.colorsEditable,
        segments: segs,
      ),
    ));
    _shadeColorsWriter?.schedule(_encodeSegmentColors(segIdx, colors));
  }

  Future<void> setBaseColors(List<LampColor> colors) async {
    final cur = state.value;
    if (cur == null) return;
    final segs = cur.base.segments.isEmpty
        ? <Segment>[]
        : [
            Segment(
              name: cur.base.segments.first.name,
              px: cur.base.segments.first.px,
              colors: colors,
            ),
            ...cur.base.segments.skip(1),
          ];
    state = AsyncData(cur.copyWith(
      base: BaseSection(
        px: cur.base.px,
        ac: cur.base.ac.clamp(0, colors.isEmpty ? 0 : colors.length - 1),
        bpp: cur.base.bpp,
        byteOrder: cur.base.byteOrder,
        colors: colors,
        knockout: cur.base.knockout,
        colorsEditable: cur.base.colorsEditable,
        segments: segs,
      ),
    ));
    _baseColorsWriter?.schedule(_encodeColors(colors));
    if (colors.isNotEmpty) {
      final acIdx = cur.base.ac.clamp(0, colors.length - 1);
      _queueSeen(base: colors[acIdx]);
    }
  }

  Future<void> setBaseAc(int index) async {
    final cur = state.value;
    if (cur == null) return;
    final clamped =
        index.clamp(0, cur.base.colors.isEmpty ? 0 : cur.base.colors.length - 1);
    state = AsyncData(cur.copyWith(
      base: BaseSection(
        px: cur.base.px,
        ac: clamped,
        bpp: cur.base.bpp,
        byteOrder: cur.base.byteOrder,
        colors: cur.base.colors,
        knockout: cur.base.knockout,
        colorsEditable: cur.base.colorsEditable,
        segments: cur.base.segments,
      ),
    ));
    // ac is part of the base settings blob, not its own characteristic; the
    // firmware picks it up on the next CHAR_SETTINGS_BLOB save. Updating
    // locally is enough for the visible session.
  }

  Future<void> setKnockoutPixel(int index, int brightness) async {
    final cur = state.value;
    if (cur == null) return;
    final clamped = brightness.clamp(0, 100);
    final next = Map<int, int>.from(cur.base.knockout);
    if (clamped == 100) {
      next.remove(index); // default — drop the entry to keep the map small
    } else {
      next[index] = clamped;
    }
    state = AsyncData(cur.copyWith(
      base: BaseSection(
        px: cur.base.px,
        ac: cur.base.ac,
        bpp: cur.base.bpp,
        byteOrder: cur.base.byteOrder,
        colors: cur.base.colors,
        knockout: next,
        colorsEditable: cur.base.colorsEditable,
        segments: cur.base.segments,
      ),
    ));
    _scheduleKnockoutWrite(index, clamped);
    // Schedule a debounced commit so the change persists on lamps
    // that expose CHAR_COMMIT. No-op on older firmware
    // (commit() returns early).
    _scheduleCommitDebounced();
  }

  void _scheduleKnockoutWrite(int index, int brightness) {
    _knockoutPending[index] = brightness;
    _knockoutTimers[index]?.cancel();
    _knockoutTimers[index] = Timer(const Duration(milliseconds: 30), () {
      final v = _knockoutPending.remove(index);
      if (v == null) return;
      // Cached BleClient avoids a `ref.read` from a Timer callback
      // that could fire after the notifier's onDispose ran.
      unawaited(_safeWriteKnockout(_ble, index, v));
    });
  }

  Future<void> _safeWriteKnockout(
      BleClient ble, int index, int brightness) async {
    try {
      // Same write-without-response semantics as the color/brightness
      // coalescers — knockout writes are also slider-rate live preview.
      await ble.write(
        _deviceId,
        BleUuids.controlService,
        BleUuids.baseKnockout,
        Uint8List.fromList([index, brightness]),
        withoutResponse: true,
      );
    } catch (_) {
      // intentionally dropped (same contract as the other live-preview writes)
    }
  }

  /// Reset every knockout entry to 100% in one pass. Cheaper than looping
  /// `setKnockoutPixel` N times because we mutate state once and skip the
  /// per-pixel debounce timers. Firmware has no clear-all sentinel, so we
  /// still write index+100 per previously-edited pixel.
  Future<void> clearKnockout() async {
    final cur = state.value;
    if (cur == null) return;
    final edited = cur.base.knockout.keys.toList();
    if (edited.isEmpty) return;
    for (final t in _knockoutTimers.values) {
      t?.cancel();
    }
    _knockoutTimers.clear();
    _knockoutPending.clear();
    state = AsyncData(cur.copyWith(
      base: BaseSection(
        px: cur.base.px,
        ac: cur.base.ac,
        bpp: cur.base.bpp,
        byteOrder: cur.base.byteOrder,
        colors: cur.base.colors,
        knockout: const {},
        colorsEditable: cur.base.colorsEditable,
        segments: cur.base.segments,
      ),
    ));
    // Serialize the per-pixel writes so the queue stays shallow.
    // Worst case is the full 144 pixels at ~30 ms each ≈ 4.5 s; only
    // hit by "Reset all" which the user doesn't repeat-spam.
    final ble = ref.read(bleClientProvider);
    unawaited(() async {
      for (final i in edited) {
        await _safeWriteKnockout(ble, i, 100);
      }
    }());
    // Schedule a debounced commit so the clear persists on lamps with CHAR_COMMIT.
    _scheduleCommitDebounced();
  }

  // ---------------------------------------------------------------------------
  // Setup-screen mutators — each call writes immediately via
  // writeSettingsBlob. name / SSID use reboot:false (instant);
  // password / advanced-LED use reboot:true (triggers reboot + ~8–12s reconnect).
  // ---------------------------------------------------------------------------

  Future<void> setLampName(String name) async {
    await _mutate(
      (s) => s.copyWith(
        lamp: LampSection(
          name: name,
          brightness: s.lamp.brightness,
          advancedEnabled: s.lamp.advancedEnabled,
          webappEnabled: s.lamp.webappEnabled,
          socialMode: s.lamp.socialMode,
          fwVersion: s.lamp.fwVersion,
          fwChannel: s.lamp.fwChannel,
        ),
      ),
      () async {
        await writeSettingsBlob({'lamp': {'name': name}}, reboot: false);
        // Inventory cache update so the AppBar LampChip title is correct
        // without waiting for a reload.
        await ref
            .read(inventoryNotifierProvider.notifier)
            .updateName(_deviceId, name);
      },
    );
  }

  Future<void> setLampAdvancedEnabled(bool v) async {
    await _mutate(
      (s) => s.copyWith(
        lamp: LampSection(
          name: s.lamp.name,
          brightness: s.lamp.brightness,
          advancedEnabled: v,
          webappEnabled: s.lamp.webappEnabled,
          socialMode: s.lamp.socialMode,
          fwVersion: s.lamp.fwVersion,
          fwChannel: s.lamp.fwChannel,
        ),
      ),
      () async {
        await writeSettingsBlob(
            {'lamp': {'advancedEnabled': v}}, reboot: false);
      },
    );
  }

  Future<void> setLampWebappEnabled(bool v) async {
    await _mutate(
      (s) => s.copyWith(
        lamp: LampSection(
          name: s.lamp.name,
          brightness: s.lamp.brightness,
          advancedEnabled: s.lamp.advancedEnabled,
          webappEnabled: v,
          socialMode: s.lamp.socialMode,
          fwVersion: s.lamp.fwVersion,
          fwChannel: s.lamp.fwChannel,
        ),
      ),
      () async {
        await writeSettingsBlob(
            {'lamp': {'webappEnabled': v}}, reboot: false);
      },
    );
  }

  /// Personality pill. Commits immediately via `writeSettingsBlob`
  /// (`reboot: false`) — no reboot required; the firmware picks up the new
  /// social mode without disconnecting.
  Future<void> setLampSocialMode(SocialMode mode) async {
    await _mutate(
      (s) => s.copyWith(
        lamp: LampSection(
          name: s.lamp.name,
          brightness: s.lamp.brightness,
          advancedEnabled: s.lamp.advancedEnabled,
          webappEnabled: s.lamp.webappEnabled,
          socialMode: mode,
          fwVersion: s.lamp.fwVersion,
          fwChannel: s.lamp.fwChannel,
        ),
      ),
      () async {
        await writeSettingsBlob(
            {'lamp': {'socialMode': mode.wire}}, reboot: false);
      },
    );
  }

  /// Wipe the lamp back to factory defaults. Writes the
  /// `{factoryReset: true}` sentinel to settings_blob (encrypted with the
  /// CURRENT password — the lamp authenticates the request before
  /// clearing). Firmware clears its NVS namespace and reboots into the
  /// awaiting-adoption state.
  ///
  /// After the expected reboot-disconnect, removes this lamp from the
  /// inventory: it no longer has a password we know, and from the user's
  /// POV it's a fresh lamp again. The caller (UI) typically navigates
  /// back to the lamp picker.
  Future<void> factoryReset() async {
    final cur = state.value;
    if (cur == null) return;
    if (!cur.connected) return;

    final ble = ref.read(bleClientProvider);
    final inv = await ref.read(inventoryNotifierProvider.future);
    final entry = inv.firstWhere(
      (l) => l.id == _deviceId,
      orElse: () =>
          throw StateError('lamp $_deviceId not in inventory'),
    );
    final pw = entry.controlPassword ?? '';

    final blob = <String, dynamic>{'factoryReset': true};
    final blobJson = jsonEncode(blob);
    final payload = pw.isEmpty
        ? Uint8List.fromList([
            LampCrypto.magicPlaintext,
            ...utf8.encode(blobJson),
          ])
        : await LampCrypto.encryptOp(
            op: blob,
            password: pw,
            saltUuid16: uuidSaltLE16(BleUuids.settingsBlob),
            charShortName: 'settingsBlob',
          );

    try {
      await ble.write(
        _deviceId,
        BleUuids.controlService,
        BleUuids.settingsBlob,
        payload,
        allowLongWrite: true,
      );
    } on BleDisconnectedException {
      // Expected: the reboot drops the link mid-write.
    } catch (e) {
      if (!isBleDisconnectError(e)) rethrow;
    }

    // Lamp is now factory-fresh — tear down everything tied to this
    // session so the reconnect machinery doesn't churn forever against
    // a lamp the user has deliberately abandoned.
    //
    // Sequence matters:
    //   1. cancel timers + the BLE-connected stream subscription so
    //      _onConnectionChange can't fire and re-schedule a reconnect;
    //   2. disconnect the link explicitly (the firmware is rebooting so
    //      this is a no-op on the wire, but it releases fbp's handle);
    //   3. remove from inventory so any UI that watches inventory drops
    //      this lamp;
    //   4. flip state to AsyncError with a sentinel so anything still
    //      watching this provider sees a terminal state rather than a
    //      stuck AsyncLoading.
    _reconnectTimer?.cancel();
    unawaited(_connSub?.cancel());
    _connSub = null;
    try {
      await ble.disconnect(_deviceId);
    } catch (_) {
      // already-disconnected / lamp rebooting — both fine.
    }
    await ref
        .read(inventoryNotifierProvider.notifier)
        .remove(_deviceId);
    state = AsyncError(
      const FactoryResetSentinel(),
      StackTrace.current,
    );
  }

  /// Called by the connect-time password prompt when the user submits a
  /// password after build() threw [LampAuthRequiredException].
  ///
  /// Writes CHAR_AUTH with the user-entered password and re-probes the auth
  /// gate by reading lampSection. On success, persists the credential to
  /// inventory (so future reconnects skip the prompt) and invalidates the
  /// provider so build() reruns cleanly with the new password in inventory.
  /// On failure, throws [LampAuthRequiredException] without mutating
  /// inventory — the dialog surfaces it inline and lets the user retry.
  ///
  /// Reuses `_ble` / `_deviceId`, which build() set before it threw. The
  /// BLE link is still alive: ControlNotifier is keepAlive, so the
  /// disconnect onDispose hasn't fired.
  Future<void> submitConnectPassword(String pw) async {
    await AuthClient(ble: _ble)
        .authenticate(deviceId: _deviceId, password: pw);
    final bytes = await _ble.readSection(_deviceId, 'lamp');
    if (bytes.isEmpty) {
      throw const LampAuthRequiredException();
    }
    await ref
        .read(inventoryNotifierProvider.notifier)
        .updatePassword(_deviceId, pw);
    ref.invalidateSelf();
  }

  /// Change the lamp's auth password. Writes a partial settings_blob with
  /// just `{lamp: {password: newPassword}}` — same path the onboarding
  /// claim uses to set the initial password. The lamp drains the write,
  /// commits the new password to NVS, and reboots; we then reconnect and
  /// reauth with the new password.
  ///
  /// Inventory is updated to the new password BEFORE the BLE write so the
  /// post-reboot reconnect picks up the new credentials. If the write
  /// fails (anything other than the expected reboot-disconnect), inventory
  /// rolls back to the old password.
  ///
  /// Mirrors `save()`'s reboot/reconnect cadence (5s delay, then reconnect
  /// + reauth + reread sections) and drives `lampSaveStatusProvider` so
  /// the UI shows "Saving changes…" instead of generic "Connecting…".
  Future<void> setLampPassword(String newPassword) async {
    final cur = state.value;
    if (cur == null) return;
    if (!cur.connected) return;

    final ble = ref.read(bleClientProvider);

    // Snapshot for rollback on real failure.
    final inv = await ref.read(inventoryNotifierProvider.future);
    final entry = inv.firstWhere(
      (l) => l.id == _deviceId,
      orElse: () =>
          throw StateError('lamp $_deviceId not in inventory'),
    );
    final oldPassword = entry.controlPassword;

    // Update inventory FIRST so the post-reboot reconnect uses the new
    // credentials. If the write fails (non-reboot error), roll back.
    await ref
        .read(inventoryNotifierProvider.notifier)
        .updatePassword(_deviceId, newPassword);

    final blob = <String, dynamic>{'lamp': {'password': newPassword}};
    final blobJson = jsonEncode(blob);
    final pw = oldPassword ?? '';
    // Accepted threat: with no prior password (factory state, post-reset) the
    // new password is written in plaintext (no shared secret yet to derive an
    // AES key), so a passive sniffer at adoption captures the new admin
    // credential. Fleet-wide mesh auth is rejected; bounded by physical
    // proximity at adoption time.
    final payload = pw.isEmpty
        ? Uint8List.fromList([
            LampCrypto.magicPlaintext,
            ...utf8.encode(blobJson),
          ])
        : await LampCrypto.encryptOp(
            op: blob,
            password: pw,
            saltUuid16: uuidSaltLE16(BleUuids.settingsBlob),
            charShortName: 'settingsBlob',
          );

    try {
      await ble.write(
        _deviceId,
        BleUuids.controlService,
        BleUuids.settingsBlob,
        payload,
        allowLongWrite: true,
      );
    } on BleDisconnectedException {
      // Expected: the reboot drops the link mid-write.
    } catch (e) {
      if (isBleDisconnectError(e)) return;
      // Genuine failure — restore the old credentials so the next
      // reconnect attempt uses what the firmware still actually has.
      await ref
          .read(inventoryNotifierProvider.notifier)
          .updatePassword(_deviceId, oldPassword);
      rethrow;
    }

    // Reuse the save()-style reconnect cadence: flip the saving banner,
    // drop state into AsyncLoading, then defer to _awaitReconnectAndReload
    // which polls connect+auth+read with backoff (replaces the brittle
    // fixed 5s delay — see the helper's docstring).
    ref.read(lampSaveStatusProvider(_deviceId).notifier).start();
    state = const AsyncLoading<ControlState>();
    unawaited(_awaitReconnectAndReload(
      ble: ble,
      password: newPassword,
      postReload: (fresh) async {
        // Same inventory-name sync as save(): user may have renamed in the
        // same edit batch that bundled the password change.
        await _inv.updateName(_deviceId, fresh.lamp.name);
      },
    ));
  }

  Future<void> setHomeSsid(String ssid) async {
    await _mutate(
      (s) => s.copyWith(
        home: HomeSection(
          ssid: ssid,
          brightness: s.home.brightness,
          enabled: s.home.enabled,
        ),
      ),
      () async {
        await writeSettingsBlob({'homeMode': {'ssid': ssid}}, reboot: false);
      },
    );
  }

  Future<void> setHomeBrightness(int brightness) async {
    final cur = state.value;
    if (cur == null) return;
    final clamped = brightness.clamp(0, 100);
    state = AsyncData(cur.copyWith(
      home: HomeSection(
        ssid: cur.home.ssid,
        brightness: clamped,
        enabled: cur.home.enabled,
      ),
    ));
    // Live-write via CHAR_BRIGHTNESS — the firmware routes the value to
    // homeMode.brightness vs lamp.brightness based on whether the app
    // has signalled it's on the Home Mode page (CHAR_HOME_MODE_FOCUS).
    // Calling setHomeBrightness while NOT on the Home Mode page would
    // (incorrectly) update lamp.brightness firmware-side; the UI only
    // wires this mutator into the Home Mode slider, so that path is
    // structurally avoided.
    _brightnessWriter?.schedule(Uint8List.fromList([clamped]));
    // Schedule debounced commit so the value persists without the Save
    // pill on lamps that expose CHAR_COMMIT.
    _scheduleCommitDebounced();
  }

  Future<void> setHomeEnabled(bool enabled) async {
    await _mutate(
      (s) => s.copyWith(
        home: HomeSection(
          ssid: s.home.ssid,
          brightness: s.home.brightness,
          enabled: enabled,
        ),
      ),
      () async {
        await writeSettingsBlob(
            {'homeMode': {'enabled': enabled}}, reboot: false);
      },
    );
  }

  /// Update px for a single segment and recompute the role total.
  /// For single-segment or old firmware (empty segments), segIdx 0 updates the role px directly.
  void setSegmentPx(String role, int segIdx, int px) {
    final cur = state.value;
    if (cur == null) return;
    final clamped = px.clamp(1, 255);
    if (role == 'shade') {
      final segs = List<Segment>.from(cur.shade.segments);
      if (segIdx < segs.length) {
        segs[segIdx] = Segment(name: segs[segIdx].name, px: clamped, colors: segs[segIdx].colors);
      }
      final rolePx = segs.isEmpty
          ? clamped
          : segs.fold<int>(0, (acc, s) => acc + s.px).clamp(1, 255);
      state = AsyncData(cur.copyWith(
        shade: ShadeSection(
          px: rolePx,
          bpp: cur.shade.bpp,
          byteOrder: cur.shade.byteOrder,
          colors: cur.shade.colors,
          colorsEditable: cur.shade.colorsEditable,
          segments: segs,
        ),
      ));
    } else {
      final segs = List<Segment>.from(cur.base.segments);
      if (segIdx < segs.length) {
        segs[segIdx] = Segment(name: segs[segIdx].name, px: clamped, colors: segs[segIdx].colors);
      }
      final rolePx = segs.isEmpty
          ? clamped
          : segs.fold<int>(0, (acc, s) => acc + s.px).clamp(1, 255);
      state = AsyncData(cur.copyWith(
        base: BaseSection(
          px: rolePx,
          ac: cur.base.ac,
          bpp: cur.base.bpp,
          byteOrder: cur.base.byteOrder,
          colors: cur.base.colors,
          knockout: cur.base.knockout,
          colorsEditable: cur.base.colorsEditable,
          segments: segs,
        ),
      ));
    }
  }

  /// Apply Advanced LED settings (px, ac) via
  /// settings_blob with reboot:true. Mirrors the setLampPassword pattern:
  /// write → firmware reboots (drops link) → reconnect ladder →
  /// reload sections. After reload, diffs the freshly-read base/shade
  /// sections against what was shipped. Returns a list of field names
  /// that didn't round-trip (empty on full success). Caller shows a
  /// "save didn't take — retry?" snackbar on non-empty mismatches.
  Future<List<String>> applyAdvancedLedsAndReboot({
    required BaseSection base,
    required ShadeSection shade,
  }) async {
    final shipped = <String, dynamic>{
      'base': {
        'px': base.px,
        'ac': base.ac,
        'colors': base.colors.map((c) => c.toHex()).toList(),
        'segments': base.segments
            .map((s) => {
                  'name': s.name,
                  'px': s.px,
                  'colors': s.colors.map((c) => c.toHex()).toList(),
                })
            .toList(),
      },
      'shade': {
        'px': shade.px,
        'colors': shade.colors.map((c) => c.toHex()).toList(),
        'segments': shade.segments
            .map((s) => {
                  'name': s.name,
                  'px': s.px,
                  'colors': s.colors.map((c) => c.toHex()).toList(),
                })
            .toList(),
      },
    };
    await writeSettingsBlob(shipped, reboot: true);

    final mismatches = <String>[];
    final ble = ref.read(bleClientProvider);
    final inv = await ref.read(inventoryNotifierProvider.future);
    final lamp = inv.firstWhere(
      (l) => l.id == _deviceId,
      orElse: () => throw StateError('lamp $_deviceId not in inventory'),
    );

    ref.read(lampSaveStatusProvider(_deviceId).notifier).start();
    state = const AsyncLoading<ControlState>();
    try {
      await _awaitReconnectAndReload(
        ble: ble,
        password: lamp.controlPassword ?? '',
        postReload: (fresh) async {
          if (fresh.base.px != base.px) mismatches.add('base.px');
          if (fresh.shade.px != shade.px) mismatches.add('shade.px');
        },
      );
    } catch (e) {
      mismatches.add('lamp did not reconnect after Advanced LED save: $e');
    }
    return mismatches;
  }

  // ---------------------------------------------------------------------------
  // Expressions
  // ---------------------------------------------------------------------------

  /// Add or update an expression. Writes via CHAR_EXPRESSION_OP — the
  /// firmware's expressionOp handler calls applyExpressionOpLocal + persistConfig,
  /// persisting the change to NVS immediately (no settings_blob write needed).
  Future<void> upsertExpression(ExpressionConfig entry) async {
    final cur = state.value;
    if (cur == null) return;

    var found = false;
    final next = <ExpressionConfig>[];
    for (final e in cur.expressions.expressions) {
      if (e.type == entry.type && e.target == entry.target) {
        next.add(entry);
        found = true;
      } else {
        next.add(e);
      }
    }
    if (!found) next.add(entry);

    state = AsyncData(cur.copyWith(
      expressions: ExpressionsSection(expressions: next),
    ));
    await _writeExpressionOp({'op': 'upsert', 'entry': entry.toJson()});
    // Firmware persists the entry to NVS via the expressionOp drain; no
    // settings_blob save needed.
  }

  /// Remove the expression keyed by (type, target). Live-previews via
  /// CHAR_EXPRESSION_OP; firmware persists to NVS immediately.
  Future<void> removeExpression({
    required String type,
    required int target,
  }) async {
    final cur = state.value;
    if (cur == null) return;
    final next = cur.expressions.expressions
        .where((e) => e.type != type || e.target != target)
        .toList();
    state = AsyncData(cur.copyWith(
      expressions: ExpressionsSection(expressions: next),
    ));
    await _writeExpressionOp({
      'op': 'remove',
      'type': type,
      'target': target,
    });
  }


  /// Live-preview an expression configuration without persisting it.
  ///
  /// Sends the firmware-expected envelope `{"a":"test_expression", "type":…,
  /// "target":…}`. Previously this sent the full ExpressionConfig JSON
  /// directly, which the firmware's BLE dispatch couldn't recognize and
  /// silently treated as `test_expression` against a non-existent type —
  /// leaving the configurator behaviors stuck in `disabled=true` until the
  /// next `test_expression_complete` (or lamp reboot). The colors and
  /// parameters are already on the lamp via the `expressionOp` upsert that
  /// preceded this call, so the envelope only needs to name the entry.
  Future<void> testExpression(ExpressionConfig entry) async {
    final ble = ref.read(bleClientProvider);
    final payload = <String, dynamic>{
      'a': 'test_expression',
      'type': entry.type,
      'target': entry.target,
    };
    final bytes = Uint8List.fromList(utf8.encode(jsonEncode(payload)));
    try {
      await ble.write(
        _deviceId,
        BleUuids.controlService,
        BleUuids.expressionTest,
        bytes,
        withoutResponse: true,
      );
    } catch (_) {
      // best-effort live preview
    }
  }

  /// Trigger a greeting from the connected lamp toward [bdAddr]. The lamp
  /// resolves the peer from its nearby list and plays its greeting; silently
  /// no-ops if the peer isn't currently sighted.
  Future<void> triggerGreet(String bdAddr) async {
    final ble = ref.read(bleClientProvider);
    final bytes = Uint8List.fromList(utf8.encode(jsonEncode({
      'a': 'triggerGreet',
      'bdAddr': bdAddr,
    })));
    try {
      await ble.write(
        _deviceId,
        BleUuids.controlService,
        BleUuids.expressionTest,
        bytes,
        withoutResponse: true,
      );
    } catch (_) {
      // best-effort
    }
  }

  /// Tells the firmware to end a `test_expression` preview and re-enable
  /// the configurator behaviors. Safe to call when no preview is in flight
  /// — firmware treats it idempotently. Called from build() (heal stuck
  /// state on connect) and from the expression editor's dispose.
  Future<void> completeExpressionTest() async {
    _zonePreviewWriter?.cancel();
    final ble = ref.read(bleClientProvider);
    await _completeExpressionTest(ble);
  }

  void previewZoneHighlight(
      int posMin, int posMax, int target, LampColor color) {
    _zonePreviewWriter?.schedule(Uint8List.fromList(utf8.encode(jsonEncode({
      'a': 'test_zone_preview',
      'posMin': posMin,
      'posMax': posMax,
      'target': target,
      'color': color.toHex(),
    }))));
  }

  /// Private form so build() can use the BleClient it already holds.
  Future<void> _completeExpressionTest(BleClient ble) async {
    try {
      await ble.write(
        _deviceId,
        BleUuids.controlService,
        BleUuids.expressionTest,
        Uint8List.fromList(
            utf8.encode('{"a":"test_expression_complete"}')),
      );
    } catch (_) {
      // best-effort — same contract as the live-preview writes
    }
  }

  Future<void> _writeExpressionOp(Map<String, dynamic> payload) async {
    final ble = ref.read(bleClientProvider);
    try {
      await ble.write(
        _deviceId,
        BleUuids.controlService,
        BleUuids.expressionOp,
        Uint8List.fromList(utf8.encode(jsonEncode(payload))),
      );
    } catch (_) {
      // best-effort
    }
  }

  Uint8List _encodeColors(List<LampColor> colors) {
    final arr = colors.map((c) => c.toHex()).toList();
    return Uint8List.fromList(utf8.encode(jsonEncode(arr)));
  }

  // Per-segment live-preview payload: {"seg":k,"colors":[...]}. The firmware
  // applies it to config.shade.segments[k] so a segment-aware behaviour (dots)
  // previews the edit. A bare array (segment 0) stays the broadcast path.
  Uint8List _encodeSegmentColors(int seg, List<LampColor> colors) {
    final map = {'seg': seg, 'colors': colors.map((c) => c.toHex()).toList()};
    return Uint8List.fromList(utf8.encode(jsonEncode(map)));
  }

  // ---------------------------------------------------------------------------
  // Connection lifecycle
  // ---------------------------------------------------------------------------

  /// Handles CHAR_STATE_NOTIFY payloads.
  /// Fields: `previewActive` (bool), `greeting` (object or absent).
  /// Older firmware emits `{}` — missing fields stay at their defaults.
  void _onStateNotify(Uint8List bytes) {
    final cur = state.value;
    if (cur == null || bytes.isEmpty) return;
    bool previewActive = false;
    GreetingState? greeting;
    try {
      final obj = jsonDecode(utf8.decode(bytes));
      if (obj is Map<String, dynamic>) {
        previewActive = obj['previewActive'] == true;
        final g = obj['greeting'];
        if (g is Map<String, dynamic> && g['active'] == true) {
          final peer = (g['peer'] as String?)?.toUpperCase() ?? '';
          final kind = (g['kind'] as String?) ?? '';
          if (peer.isNotEmpty) greeting = GreetingState(peer: peer, kind: kind);
        }
      }
    } catch (_) {
      return;
    }
    if (previewActive == cur.previewActive && greeting == cur.greeting) return;
    state = AsyncData(cur.copyWith(previewActive: previewActive, greeting: greeting));
  }

  void _onConnectionChange(bool isConnected) {
    final cur = state.value;
    if (cur == null) return;
    if (isConnected && !cur.connected) {
      // fbp's connectionState emitted `true` mid-reconnect — but the
      // GATT services aren't necessarily discovered yet (observed on
      // hardware: a 4-second window where writes failed with
      // "primary service not found" before the canary lands).
      //
      // Don't flip the UI to "connected" on this edge — _tryReconnect's
      // canary read is the truthful "the link can actually be used"
      // signal. Kick _tryReconnect immediately rather than waiting for
      // the soft 500ms reconnect timer; the in-flight guard prevents
      // doubling up with a timer-scheduled run that's already running.
      _reconnectTimer?.cancel();
      unawaited(_tryReconnect());
      return;
    }
    if (!isConnected && cur.connected) {
      state = AsyncData(cur.copyWith(connected: false));
      ref.read(advancedSessionProvider(_deviceId).notifier).disable();
      _scheduleReconnect();
    }
  }

  /// Fires a no-op GATT read against the lamp to verify the link is
  /// actually alive. Used by the foreground-resume listener: fbp may
  /// still report `isConnected == true` for a connection the OS killed
  /// while the app was backgrounded, but any real I/O immediately
  /// throws BleDisconnectedException. We surface that as the disconnect
  /// edge we never observed, and the existing _onConnectionChange path
  /// schedules a reconnect.
  ///
  /// No-op when:
  ///   - notifier state is still loading / errored (nothing connected yet)
  ///   - we're already in a reconnect cycle (banner showing attempts)
  Future<void> _probeLink() async {
    final cur = state.value;
    if (cur == null || !cur.connected) return;
    final ble = ref.read(bleClientProvider);
    if (!ble.isConnected(_deviceId)) {
      // fbp itself has dropped the link — surface the missing false edge.
      _onConnectionChange(false);
      return;
    }
    try {
      // Cheap real GATT op: read the lamp section (small payload, part
      // of the cold-start sweep, no side effects). If this throws a
      // disconnect-shaped error, the slot is zombified.
      await ble.readSection(_deviceId, 'lamp');
    } on BleDisconnectedException {
      // The await above is the dispose window: the user can switch
      // lamps mid-probe, disposing the notifier. Touching state after
      // that throws. The `mounted` check matches the pattern in
      // _tryReconnect's catch.
      if (!ref.mounted) return;
      _onConnectionChange(false);
    } catch (_) {
      // Any other error (transient read failure, encryption etc.) is
      // not a clean disconnect signal. Don't disturb the connection
      // state — the next user action will surface a real failure if
      // the link is actually dead.
    }
  }

  /// Reconnect attempt at which we escalate to `cycleAdapter`. The soft
  /// reconnect ladder is good for clean link drops (lamp reboot,
  /// transient RF loss); after this many failures we assume the
  /// Android `gatts_if` slot has zombified and force a soft-cycle
  /// (explicit disconnect + delay + reconnect) before the next attempt.
  /// Pre-this-attempt, plain reconnect; on this attempt, cycle then
  /// reconnect.
  static const int _cycleAdapterAttempt = 3;

  void _scheduleReconnect() {
    final cur = state.value;
    if (cur == null) return;
    final attempt = cur.reconnectAttempt;
    // Park after the cap: without it a wandered-off lamp keeps polling every
    // 8s indefinitely, draining battery. User can re-tap the lamp in MyLamps
    // to reset the attempt counter.
    if (attempt >= _maxReconnectAttempts) {
      state = AsyncError(
        const BleNotConnected('reconnect cap reached'),
        StackTrace.current,
      );
      return;
    }
    final delayMs = _reconnectDelays[
        attempt < _reconnectDelays.length ? attempt : _reconnectDelays.length - 1];
    _reconnectTimer?.cancel();
    _reconnectTimer =
        Timer(Duration(milliseconds: delayMs), _tryReconnect);
    state = AsyncData(cur.copyWith(reconnectAttempt: attempt + 1));
  }

  Future<void> _tryReconnect() async {
    if (_reconnectInFlight) {
      return;
    }
    _reconnectInFlight = true;
    final ble = ref.read(bleClientProvider);
    try {
      final attempt = state.value?.reconnectAttempt ?? 0;
      // Tier 3: if soft reconnects have failed enough times, soft-cycle
      // the slot before the next connect. fbp.connect() returning
      // success on a dead slot is the documented gatts_if-leak
      // fingerprint that "force-stop fixes it" reports map onto.
      if (attempt >= _cycleAdapterAttempt) {
        await ble.cycleAdapter(_deviceId);
      }
      await ble.connect(_deviceId);
      // Re-auth so subsequent writes get past the firmware's auth gate.
      final inv = await ref.read(inventoryNotifierProvider.future);
      final lamp = inv.firstWhere(
        (l) => l.id == _deviceId,
        orElse: () => throw StateError('lamp $_deviceId not in inventory'),
      );
      await AuthClient(ble: ble)
          .authenticate(deviceId: _deviceId, password: lamp.controlPassword);
      // Same canary as build(): if the firmware still returns empty bytes
      // after our auth attempt, the stored password no longer works (e.g.
      // it was changed on another device). Drop to error so the UI re-
      // prompts instead of leaving the user in a silent-write-rejected
      // state with the banner clearing as if everything was fine.
      final canaryBytes = await ble.readSection(_deviceId, 'lamp');
      if (canaryBytes.isEmpty) {
        state = AsyncError(
            const LampAuthRequiredException(), StackTrace.current);
        return;
      }
      // Canary succeeded → the link is fully usable (GATT connected
      // AND services discovered AND auth restored). Pull every section
      // fresh from the lamp before flipping the UI to connected — during
      // the disconnect window the firmware state could have changed via
      // the webapp, a lamp reboot, or a mesh override, and the locally-
      // cached state is stale. The lamp is authoritative.
      //
      // Trade-off: if the user dragged a slider mid-disconnect (the
      // live-preview writes failed silently during the gap), that input
      // is lost. The alternative — push the stale local cache — would
      // overwrite any genuine lamp-side change with old data, which
      // breaks trust in a way the missing-gesture case doesn't.
      //
      // Bail if disposed during the awaits — touching state would throw.
      // The notifier dispose path already cancels everything we care about.
      final fresh = await _readSections(ble);
      if (!ref.mounted) return;
      _reconnectTimer?.cancel();
      state = AsyncData(fresh.copyWith(connected: true, reconnectAttempt: 0));
    } catch (_) {
      // Bail if the notifier was disposed while we were awaiting — touching
      // `state` after dispose throws. This happens in tests that
      // dispose the ProviderContainer while a reconnect is mid-flight,
      // and could also happen in production if the user navigates
      // away (lamp picker → other lamp) during a slow reconnect.
      if (!ref.mounted) {
        _reconnectInFlight = false;
        return;
      }
      // Clear the flag BEFORE scheduling the next attempt — the timer
      // is scheduled via _scheduleReconnect and its `_tryReconnect`
      // call will check `_reconnectInFlight` on entry. If finally
      // hadn't yet run, that next call would incorrectly skip itself
      // and the ladder would stall on its current attempt count.
      _reconnectInFlight = false;
      _scheduleReconnect();
    } finally {
      // Defense-in-depth: the catch branches above clear the flag
      // explicitly; finally covers the success path (and any future
      // catch branch that forgets to).
      _reconnectInFlight = false;
    }
  }

}
