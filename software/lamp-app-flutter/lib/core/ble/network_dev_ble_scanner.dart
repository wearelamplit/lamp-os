import 'dart:async';
import 'ble_scanner.dart';
import 'bridge_transport.dart';

class NetworkDevBleScanner implements BleScanner {
  NetworkDevBleScanner(this._t);
  final BridgeTransport _t;
  // Created once, never closed. autoDispose re-listens the same overridden
  // instance on enter→exit→enter; closing here would permanently silence it.
  final _ctrl = StreamController<BleAdvertisement>.broadcast();
  StreamSubscription<Map<String, dynamic>>? _sub;
  bool _running = false;

  @override
  Stream<BleAdvertisement> results() => _ctrl.stream;

  @override
  Future<void> start() async {
    if (_running) return;
    _running = true;
    _sub = _t.openChannel('/scan').listen((m) {
      final ad = parseLampAdvertisement(
        manufacturerData: (m['manufacturerData'] as Map).map(
            (k, v) => MapEntry(int.parse(k as String), (v as List).cast<int>())),
        advName: m['advName'] as String? ?? '',
        platformName: m['platformName'] as String? ?? '',
        remoteId: m['remoteId'] as String,
        rssi: m['rssi'] as int,
      );
      if (ad != null) _ctrl.add(ad);
    });
  }

  @override
  Future<void> stop() async {
    if (!_running) return;
    _running = false;
    await _sub?.cancel();
    _sub = null;
  }
}
