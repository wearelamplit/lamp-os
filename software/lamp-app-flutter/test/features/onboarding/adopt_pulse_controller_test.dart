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
  const base = LampColor(r: 0x20, g: 0x40, b: 0x60, w: 0);

  List<Uint8List> baseWrites(InMemoryBleClient ble) =>
      ble.writesTo(deviceId, BleUuids.baseColors);
  List<Uint8List> sessionWrites(InMemoryBleClient ble) =>
      ble.writesTo(deviceId, BleUuids.editSession);

  group('AdoptPulseController', () {
    test('start connects, opens editSession, writes bright baseColor', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade, base);
        async.flushMicrotasks();

        expect(ble.isConnected(deviceId), isTrue);

        // editSession open: [0x01, 1]
        expect(sessionWrites(ble), hasLength(1));
        expect(sessionWrites(ble).first, equals([0x01, 1]));

        // baseColors: a JSON array of one hex string
        expect(baseWrites(ble), hasLength(1));
        final decoded = jsonDecode(utf8.decode(baseWrites(ble).first)) as List;
        expect(decoded.first, isA<String>());

        ctrl.stop();
        async.flushMicrotasks();
      });
    });

    test('timer alternates bright/dim every 600ms', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade, base);
        async.flushMicrotasks();

        final after0 = baseWrites(ble).length; // 1 (initial bright)

        async.elapse(const Duration(milliseconds: 600));
        expect(baseWrites(ble).length, greaterThan(after0));

        async.elapse(const Duration(milliseconds: 600));
        expect(baseWrites(ble).length, greaterThan(after0 + 1));

        ctrl.stop();
        async.flushMicrotasks();
      });
    });

    test('stop restores base, closes editSession, disconnects', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade, base);
        async.flushMicrotasks();

        ctrl.stop();
        async.flushMicrotasks();

        // editSession close written last: [0x01, 0]
        expect(sessionWrites(ble).last, equals([0x01, 0]));

        // base restore written: original base color hex
        final lastBase = jsonDecode(utf8.decode(baseWrites(ble).last)) as List;
        expect(lastBase.first, equals(base.toHex()));

        expect(ble.isConnected(deviceId), isFalse);
      });
    });

    test('stop is idempotent — second call does not throw', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade, base);
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

        // start() awaits connect(), so its continuation (editSession + baseColors
        // + timer install) is a scheduled microtask — stop() runs first and sets
        // _stopped=true before that continuation fires.
        ctrl.start(deviceId, shade, base);
        ctrl.stop();
        async.flushMicrotasks(); // start continuation: if (_stopped) return;

        final countAfterStop = baseWrites(ble).length;

        // Advance 5 × 600ms — a leaked timer would fire here.
        async.elapse(const Duration(milliseconds: 3000));

        expect(baseWrites(ble).length, equals(countAfterStop));
      });
    });

    test('double start does not double pulse cadence', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        final ctrl = AdoptPulseController(ble);

        ctrl.start(deviceId, shade, base);
        async.flushMicrotasks();

        // Second start cancels the first timer; only one timer should be active.
        ctrl.start(deviceId, shade, base);
        async.flushMicrotasks();

        final countBefore = baseWrites(ble).length;

        async.elapse(const Duration(milliseconds: 600));

        final delta = baseWrites(ble).length - countBefore;
        // Single timer fires once per interval — not twice.
        expect(delta, 1);

        ctrl.stop();
        async.flushMicrotasks();
      });
    });
  });
}
