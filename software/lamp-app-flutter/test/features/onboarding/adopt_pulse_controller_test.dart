import 'dart:convert';
import 'dart:typed_data';

import 'package:fake_async/fake_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/onboarding/application/adopt_pulse_controller.dart';

import '../../_support/in_memory_ble_client.dart';

void main() {
  const deviceId = 'lamp-test-01';
  const shade = LampColor(r: 0x40, g: 0x80, b: 0xFF, w: 0);

  List<Uint8List> exprWrites(InMemoryBleClient ble) =>
      ble.writesTo(deviceId, BleUuids.expressionTest);

  Map<String, dynamic> decodeExpr(Uint8List bytes) =>
      jsonDecode(utf8.decode(bytes)) as Map<String, dynamic>;

  group('AdoptPulseController', () {
    test('start connects and writes test_expression pulse payload', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade);
        async.flushMicrotasks();

        expect(ble.isConnected(deviceId), isTrue);

        final writes = exprWrites(ble);
        expect(writes, hasLength(1));

        final payload = decodeExpr(writes.first);
        expect(payload['a'], equals('test_expression'));
        expect(payload['type'], equals('pulse'));
        expect(payload['target'], equals(2));
        expect(payload['colors'], isA<List<dynamic>>());
        expect((payload['colors'] as List<dynamic>).first, isA<String>());

        ctrl.stop();
        async.flushMicrotasks();
      });
    });

    test('timer re-fires expressionTest on 1500ms cadence', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade);
        async.flushMicrotasks();

        final after0 = exprWrites(ble).length; // 1 (initial write)

        async.elapse(const Duration(milliseconds: 1500));
        expect(exprWrites(ble).length, greaterThan(after0));

        async.elapse(const Duration(milliseconds: 1500));
        expect(exprWrites(ble).length, greaterThan(after0 + 1));

        ctrl.stop();
        async.flushMicrotasks();
      });
    });

    test('stop writes test_expression_complete and disconnects', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade);
        async.flushMicrotasks();

        ctrl.stop();
        async.flushMicrotasks();

        final writes = exprWrites(ble);
        expect(writes, isNotEmpty);
        final last = decodeExpr(writes.last);
        expect(last['a'], equals('test_expression_complete'));

        expect(ble.isConnected(deviceId), isFalse);
      });
    });

    test('stop is idempotent — second call does not throw', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade);
        async.flushMicrotasks();

        ctrl.stop();
        async.flushMicrotasks();

        expect(
            () {
              ctrl.stop();
              async.flushMicrotasks();
            },
            returnsNormally);
      });
    });

    test('stop before timer installs does not leak timer', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        // start() awaits connect(), so its continuation (_writePulse + timer
        // install) is a scheduled microtask — stop() runs first and sets
        // _stopped=true before that continuation fires.
        ctrl.start(deviceId, shade);
        ctrl.stop();
        async.flushMicrotasks();

        final countAfterStop = exprWrites(ble).length;

        // Advance 3s (2 × 1500ms) — a leaked timer would fire here.
        async.elapse(const Duration(milliseconds: 3000));

        expect(exprWrites(ble).length, equals(countAfterStop));
      });
    });

    test('double start does not double pulse cadence', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade);
        async.flushMicrotasks();

        // Second start cancels the first timer; only one timer should be active.
        ctrl.start(deviceId, shade);
        async.flushMicrotasks();

        final countBefore = exprWrites(ble).length;

        async.elapse(const Duration(milliseconds: 3000));

        final delta = exprWrites(ble).length - countBefore;
        // Single 1500ms timer fires 2 times in 3000ms — not 4 (double timer).
        expect(delta, 2);

        ctrl.stop();
        async.flushMicrotasks();
      });
    });
  });
}
