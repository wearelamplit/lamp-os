import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/network_dev_ble_scanner.dart';
import '../../_support/fake_bridge_transport.dart';

void main() {
  test('survives start/stop/start and still yields results', () async {
    final t = FakeBridgeTransport();
    final s = NetworkDevBleScanner(t);
    final got = <String>[];
    s.results().listen((a) => got.add(a.name));

    await s.start();
    await s.stop();       // must NOT close the results controller
    await s.start();      // reuse the same instance

    t.pushChannel('/scan', {
      'manufacturerData': {'42069': [255, 0, 0, 2]}, // 0xA455, mesh cap
      'advName': 'Flora', 'platformName': '', 'remoteId': 'AA:BB:CC:DD:EE:FF', 'rssi': -50,
    });
    await Future<void>.delayed(const Duration(milliseconds: 20));
    expect(got, contains('Flora'));
  });
}
