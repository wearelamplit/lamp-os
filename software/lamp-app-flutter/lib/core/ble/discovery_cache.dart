import 'dart:async';

import 'ble_timing.dart';

/// Per-device cache of a link's discovered GATT services. A miss runs
/// [discover] once, shared by every resolve that arrives while it runs, and
/// records how long it took; a hit costs nothing. A failure is not cached.
/// After [remove], a discovery still in flight answers only its own waiters.
class DiscoveryCache<S> {
  final _byDevice = <String, S>{};
  final _inFlight = <String, Future<S>>{};

  Future<S> resolve(String deviceId, Future<S> Function() discover) {
    final hit = _byDevice[deviceId];
    if (hit != null) return Future.value(hit);
    final pending = _inFlight[deviceId];
    if (pending != null) return pending;
    final started = bleTimings.now();
    final done = Completer<S>();
    final run = done.future;
    _inFlight[deviceId] = run;
    bool current() => identical(_inFlight[deviceId], run);
    run.then(
      (found) {
        if (!current()) return;
        _inFlight.remove(deviceId);
        _byDevice[deviceId] = found;
      },
      onError: (Object _) {
        if (current()) _inFlight.remove(deviceId);
      },
    );
    run
        .then<void>(
          (_) => bleTimings.discovered(deviceId, started),
          onError: (Object e) =>
              bleTimings.discovered(deviceId, started, error: e),
        )
        .ignore();
    // Registered first, so a discover that re-enters resolve or remove sees
    // this run.
    done.complete(Future.sync(discover));
    return run;
  }

  void remove(String deviceId) {
    _byDevice.remove(deviceId);
    _inFlight.remove(deviceId);
  }
}
