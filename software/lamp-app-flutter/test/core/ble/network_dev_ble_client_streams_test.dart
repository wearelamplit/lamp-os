import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/network_dev_ble_client.dart';
import '../../_support/fake_bridge_transport.dart';

void main() {
  test('watchConnected emits current state immediately on listen', () async {
    final t = FakeBridgeTransport();
    final c = NetworkDevBleClient(t);
    await c.connect('dev');
    // Already connected: firstWhere((v)=>v) must complete, NOT hang.
    final v = await c.watchConnected('dev')
        .firstWhere((connected) => connected)
        .timeout(const Duration(seconds: 1));
    expect(v, isTrue);
  });

  test('cycleAdapter emits false then reconnects true', () async {
    final t = FakeBridgeTransport();
    final c = NetworkDevBleClient(t);
    await c.connect('dev');
    final seen = <bool>[];
    final sub = c.watchConnected('dev').listen(seen.add);
    await c.cycleAdapter('dev');
    await Future<void>.delayed(const Duration(milliseconds: 50));
    await sub.cancel();
    expect(seen.first, isTrue);          // seed
    expect(seen.contains(false), isTrue); // drop
    expect(seen.last, isTrue);            // reconnect
  });

  test('watchConnected reacts to bridge /watch drop edge', () async {
    final t = FakeBridgeTransport();
    final c = NetworkDevBleClient(t);
    await c.connect('dev');
    final seen = <bool>[];
    final sub = c.watchConnected('dev').listen(seen.add);
    t.pushChannel('/watch/dev', {'connected': false});
    await Future<void>.delayed(const Duration(milliseconds: 50));
    await sub.cancel();
    expect(seen.contains(false), isTrue);
    expect(c.isConnected('dev'), isFalse);
  });
}
