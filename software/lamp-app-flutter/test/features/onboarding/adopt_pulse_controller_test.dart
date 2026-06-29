import 'dart:convert';

import 'package:fake_async/fake_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/onboarding/application/adopt_pulse_controller.dart';

import '../../_support/in_memory_ble_client.dart';

void main() {
  const deviceId = 'lamp-test-01';
  const base = LampColor(r: 0x40, g: 0x80, b: 0xFF, w: 0);

  group('AdoptPulseController', () {
    test('start connects and pulses on each 1500ms tick', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, base);
        async.flushMicrotasks();

        // Immediate pulse on connect
        final writes1 = ble.writesTo(deviceId, BleUuids.expressionTest);
        expect(writes1.length, 1);
        final p1 = jsonDecode(utf8.decode(writes1.first)) as Map<String, dynamic>;
        expect(p1['a'], 'test_expression');
        expect(p1['type'], 'pulse');

        // Second tick
        async.elapse(const Duration(milliseconds: 1500));
        expect(ble.writesTo(deviceId, BleUuids.expressionTest).length, greaterThanOrEqualTo(2));

        // Third tick
        async.elapse(const Duration(milliseconds: 1500));
        expect(ble.writesTo(deviceId, BleUuids.expressionTest).length, greaterThanOrEqualTo(3));

        ctrl.stop();
        async.flushMicrotasks();
      });
    });

    test('stop sends test_expression_complete and disconnects', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, base);
        async.flushMicrotasks();

        ctrl.stop();
        async.flushMicrotasks();

        final writes = ble.writesTo(deviceId, BleUuids.expressionTest);
        final last = jsonDecode(utf8.decode(writes.last)) as Map<String, dynamic>;
        expect(last['a'], 'test_expression_complete');
        expect(ble.isConnected(deviceId), isFalse);
      });
    });

    test('stop is idempotent — second call does not throw', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, base);
        async.flushMicrotasks();

        ctrl.stop();
        async.flushMicrotasks();

        // Second stop must not throw
        expect(() { ctrl.stop(); async.flushMicrotasks(); }, returnsNormally);
      });
    });
  });
}
