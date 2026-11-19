import 'dart:async';

import 'ble_timing.dart';

/// Per-device cache of a link's discovered GATT services. A miss runs
/// [discover] once, shared by every resolve that arrives while it runs, and
/// records how long it took; a hit costs nothing. A failure is not cached.
/// After [remove], a discovery still in flight answers only its own waiters.
class DiscoveryCache<S> {
  final _byDevice = <String, S>{};
  final _inFlight = <String, Completer<S>>{};

  Future<S> resolve(String deviceId, Future<S> Function() discover) {
    final hit = _byDevice[deviceId];
    if (hit != null) return Future.value(hit);
    final pending = _inFlight[deviceId];
    if (pending != null) return pending.future;
    final run = _inFlight[deviceId] = Completer<S>();
    _discover(deviceId, discover, run);
    return run.future;
  }

  Future<void> _discover(
    String deviceId,
    Future<S> Function() discover,
    Completer<S> run,
  ) async {
    final started = bleTimings.now();
    bool current() => identical(_inFlight[deviceId], run);
    final S found;
    try {
      found = await discover();
    } catch (e, st) {
      bleTimings.discovered(deviceId, started, error: e);
      if (current()) _inFlight.remove(deviceId);
      run.completeError(e, st);
      return;
    }
    bleTimings.discovered(deviceId, started);
    if (current()) {
      _inFlight.remove(deviceId);
      _byDevice[deviceId] = found;
    }
    run.complete(found);
  }

  void remove(String deviceId) {
    _byDevice.remove(deviceId);
    _inFlight.remove(deviceId);
  }
}
