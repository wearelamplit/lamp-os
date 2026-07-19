import 'dart:async';

import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/ble_scanner.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../domain/nearby_lamp.dart';

part 'nearby_lamps_notifier.g.dart';

const _staleAfter = Duration(seconds: 30);

/// How often to sweep the roster and prune stale entries. Without it, a lamp
/// that goes silently out of range while no other lamp emits an adv would
/// stick in `state` forever (`_onAd` only rebuilds when an adv lands).
const _pruneInterval = Duration(seconds: 5);

/// Minimum gap between back-to-back prewarm requests for the same device id.
/// 10 s comfortably covers the BLE connect handshake + service discovery so a
/// lamp's 16-30 Hz adv stream doesn't re-trigger prewarm before it completes.
const _prewarmDebounce = Duration(seconds: 10);

/// Max rate `state` is re-emitted to consumers. Leading-edge: the first change
/// in a window emits immediately (a new lamp appears within ~16 ms); later
/// changes coalesce into one trailing emit. 1000 ms keeps a 22-lamp fleet's
/// ~44 advs/sec from rebuilding every roster watcher at 44 Hz.
const _stateEmitWindow = Duration(milliseconds: 1000);

// Scoped to consumers: when no screen watches `nearbyLampsNotifierProvider`
// the notifier auto-disposes and its `ref.onDispose` calls `scanner.stop()`,
// handing the scan budget back to Android.
@Riverpod(name: 'bleScannerProvider')
BleScanner bleScanner(Ref ref) => FbpBleScanner();

@Riverpod(name: 'nearbyLampsNotifierProvider')
class NearbyLampsNotifier extends _$NearbyLampsNotifier {
  StreamSubscription<BleAdvertisement>? _sub;
  Timer? _pruneTimer;
  // Per-lamp prewarm rate-limit. Keyed by adv `id` (the BLE device id),
  // value is the wall-clock ms of the last prewarm dispatch.
  final Map<String, int> _lastPrewarmMsById = {};
  // Latest computed roster waiting to be flushed to `state`. Null when
  // nothing is pending. Used by the leading-edge emit throttle.
  List<NearbyLamp>? _pendingRoster;
  // True while the throttle window is open: accumulate into `_pendingRoster`
  // instead of emitting. The trailing-edge timer flushes when it closes.
  Timer? _emitWindowTimer;

  @override
  List<NearbyLamp> build() {
    final scanner = ref.read(bleScannerProvider);
    scanner.start();
    _sub = scanner.results().listen(_onAd);
    ref.onDispose(() {
      _sub?.cancel();
      _pruneTimer?.cancel();
      _pruneTimer = null;
      _emitWindowTimer?.cancel();
      _emitWindowTimer = null;
      _pendingRoster = null;
      scanner.stop();
    });
    return const [];
  }

  void _onAd(BleAdvertisement ad) {
    final now = DateTime.now().millisecondsSinceEpoch;
    final updated = NearbyLamp(
      id: ad.id,
      name: ad.name,
      rssi: ad.rssi,
      serviceUuids: ad.serviceUuids,
      baseRgb: ad.baseRgb,
      shadeRgb: ad.shadeRgb,
      lastSeenEpochMs: now,
      isMesh: ad.isMesh,
      configured: ad.configured,
    );
    // Build the next-roster against whatever's MOST CURRENT: either the
    // pending list waiting to flush, or the live state if no window is
    // open. Without checking _pendingRoster here, a burst of advs in a
    // single window would each rebuild against stale `state`.
    final base = _pendingRoster ?? state;
    final next = [
      for (final l in base)
        if (l.id != ad.id &&
            now - l.lastSeenEpochMs < _staleAfter.inMilliseconds)
          l,
      updated,
    ];
    _sortRoster(next);
    _scheduleStateEmit(next);
    // Lazy-start the prune timer on first adv. Widget tests that don't pipe
    // through the scanner never create a timer, so they don't trip the test
    // framework's `!timersPending` check.
    _pruneTimer ??= Timer.periodic(_pruneInterval, (_) => _prune());
    _maybePrewarm(ad, now);
  }

  /// Leading-edge emit throttle. Without it, every adv on a 22-lamp fleet
  /// (~44 Hz) reassigned `state` and rebuilt every consumer not using
  /// `.select()`. First adv in a window emits immediately; later advs land in
  /// `_pendingRoster` and flush when the window closes (skipped if unchanged).
  void _scheduleStateEmit(List<NearbyLamp> next) {
    if (_emitWindowTimer == null) {
      // Leading edge: emit + open the window.
      state = next;
      _pendingRoster = null;
      _emitWindowTimer = Timer(_stateEmitWindow, _onEmitWindowClose);
    } else {
      // Inside the window: stash the latest roster; flush when it closes.
      _pendingRoster = next;
    }
  }

  void _onEmitWindowClose() {
    _emitWindowTimer = null;
    final pending = _pendingRoster;
    if (pending == null) return;
    _pendingRoster = null;
    // In steady state every adv mutates rssi, so every flush would emit and
    // rebuild every unselect-watching consumer. NearbyLamp is @freezed (== on
    // all fields); skip the assignment when the pending roster already matches.
    if (_listEquals(pending, state)) return;
    state = pending;
  }

  /// Stable roster order: bucket by RSSI tier (20 dBm bands so typical ±5 dBm
  /// jitter doesn't flip neighbours at a boundary), then alphabetical by name
  /// within a bucket. Name beats recency/raw-RSSI as the tiebreaker because
  /// those re-flip on every adv, defeating stability. 20 dBm collapses "same
  /// room or one wall over" into one bucket; proximity ordering still
  /// separates far-end-of-house peers.
  static void _sortRoster(List<NearbyLamp> list) {
    list.sort((a, b) {
      // RSSI is negative; less-negative = closer. Bucket by 20 dBm,
      // sort DESC so bucket -3 (-60..-79 dBm) precedes bucket -4
      // (-80..-99 dBm) etc.
      final bucketA = a.rssi ~/ 20;
      final bucketB = b.rssi ~/ 20;
      if (bucketA != bucketB) return bucketB.compareTo(bucketA);
      return a.name.toLowerCase().compareTo(b.name.toLowerCase());
    });
  }

  /// Lightweight content equality for the roster. Lists of @freezed
  /// NearbyLamp; element `==` checks every field.
  static bool _listEquals(List<NearbyLamp> a, List<NearbyLamp> b) {
    if (identical(a, b)) return true;
    if (a.length != b.length) return false;
    for (var i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }

  /// Prewarm a GATT connection to a paired lamp as soon as its adv lands, so
  /// by tap time the connect handshake + service discovery are already done
  /// (`control_notifier` short-circuits its `connect()` when `isConnected`).
  /// Saves ~300-900 ms of perceived tap-to-interactive latency. Rate-limited
  /// per device id by `_prewarmDebounce`; `BleClient.prewarm` also enforces a
  /// single-slot mutex across all ids.
  void _maybePrewarm(BleAdvertisement ad, int nowMs) {
    // Use the adv fields directly; the adv may have landed in `_pendingRoster`
    // but not yet `state` during the emit throttle.
    final deviceId = ad.id;
    final inv = ref.read(inventoryNotifierProvider).value;
    if (inv == null) return;
    final invNotifier = ref.read(inventoryNotifierProvider.notifier);
    final isPaired = inv.any((l) => l.id == deviceId);
    if (!isPaired) {
      // Cross-platform / iOS-reinstall reconciliation: the new id
      // doesn't match any inventory entry. Check whether the adv's
      // name + colors uniquely identify a paired lamp under an older
      // platform id. If so, the inventory entry's id is updated in
      // place and subsequent advs hit the fast `isPaired = true`
      // branch above.
      unawaited(invNotifier.reconcileIdByIdentity(
        newId: deviceId,
        name: ad.name,
        baseRgb: ad.baseRgb,
        shadeRgb: ad.shadeRgb,
      ));
      return;
    }
    // Update inventory "last seen" too. Without this, My Lamps says
    // "3 weeks ago" for a paired lamp that the user has been seeing
    // continuously since they opened the app. InventoryNotifier
    // self-caps at one write per 30s per id, so it's safe to call
    // unconditionally on every adv.
    unawaited(invNotifier.touchLastSeen(deviceId));
    // Cache the observed mesh capability so the route resolver can
    // route correctly even when the lamp is out of range. Self-caps
    // at "only writes when the value changes" so this is also safe
    // to call on every adv.
    unawaited(invNotifier.rememberMeshState(deviceId, isMesh: ad.isMesh));
    final last = _lastPrewarmMsById[deviceId] ?? 0;
    if (nowMs - last < _prewarmDebounce.inMilliseconds) return;
    _lastPrewarmMsById[deviceId] = nowMs;
    final ble = ref.read(bleClientProvider);
    // Fire-and-forget. The BleClient impl guarantees this never throws
    // and handles its own single-slot mutex + connection cleanup on
    // failure.
    unawaited(ble.prewarm(deviceId));
  }

  /// Drop stale entries from `state`. Called both implicitly by
  /// `_onAd` (which filters during its rebuild) and on a 5-second
  /// periodic timer so lamps that disappear silently still get
  /// pruned without waiting for another lamp's adv.
  void _prune() {
    final now = DateTime.now().millisecondsSinceEpoch;
    final base = _pendingRoster ?? state;
    final next = [
      for (final l in base)
        if (now - l.lastSeenEpochMs < _staleAfter.inMilliseconds) l,
    ];
    if (next.length != base.length) {
      _scheduleStateEmit(next);
    }
  }
}
