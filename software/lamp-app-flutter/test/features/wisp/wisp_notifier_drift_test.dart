import 'dart:convert';
import 'dart:typed_data';

import 'package:fake_async/fake_async.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const lampId = 'drift-lamp';

  setUp(() => SharedPreferences.setMockInitialValues({}));

  ProviderContainer makeContainer(InMemoryBleClient ble) {
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    return c;
  }

  Future<void> primeStatus(InMemoryBleClient ble, String json) async {
    await ble.connect(lampId);
    await ble.write(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(utf8.encode(json)),
    );
  }

  group('WispNotifier setDrift debounce', () {
    test('rapid calls collapse to one write with the last values', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      final before = ble.writesTo(lampId, BleUuids.wispOp).length;

      fakeAsync((fa) {
        n.setDrift(1000, 50);
        n.setDrift(2000, 75);
        fa.elapse(const Duration(milliseconds: 301));
        fa.flushMicrotasks();
      });

      final writes = ble.writesTo(lampId, BleUuids.wispOp);
      expect(writes.length, before + 1);
      final decoded =
          jsonDecode(utf8.decode(writes.last)) as Map<String, dynamic>;
      expect(decoded['op'], 'setDrift');
      expect(decoded['intervalMs'], 2000);
      expect(decoded['fadePct'], 75);
    });

    test('flushDrift writes immediately with no duplicate after the window',
        () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"AA:BB:CC:DD:EE:FF"}');
      final c = makeContainer(ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      final before = ble.writesTo(lampId, BleUuids.wispOp).length;

      fakeAsync((fa) {
        n.setDrift(3000, 80);
        n.flushDrift();
        fa.flushMicrotasks();
        fa.elapse(const Duration(milliseconds: 400));
        fa.flushMicrotasks();
      });

      final writes = ble.writesTo(lampId, BleUuids.wispOp);
      expect(writes.length, before + 1);
      final decoded =
          jsonDecode(utf8.decode(writes.last)) as Map<String, dynamic>;
      expect(decoded['op'], 'setDrift');
      expect(decoded['intervalMs'], 3000);
      expect(decoded['fadePct'], 80);
    });
  });
}
