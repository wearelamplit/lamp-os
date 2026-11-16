import 'ble_timing.dart';

/// Per-device cache of a link's discovered GATT services. A miss runs
/// [discover] and records how long it took; a hit costs nothing.
class DiscoveryCache<S> {
  final _byDevice = <String, S>{};

  Future<S> resolve(String deviceId, Future<S> Function() discover) async {
    final hit = _byDevice[deviceId];
    if (hit != null) return hit;
    final started = bleTimings.now();
    final found = await discover();
    bleTimings.discovered(deviceId, started);
    return _byDevice[deviceId] = found;
  }

  void remove(String deviceId) => _byDevice.remove(deviceId);
}
