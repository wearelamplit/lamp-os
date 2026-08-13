import 'dart:typed_data';

import 'package:fake_async/fake_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/write_coalescer.dart';

/// Throttle-semantics tests for [WriteCoalescer] and [KeyedWriteCoalescer].
/// Pacing is driven by the debounce timer, not by the previous write's ACK,
/// so the lamp sees a steady cadence during a continuous gesture even when
/// the BLE link is slow (WIDE conn) or another characteristic's WRITE-with-
/// response is in fbp's queue ahead of the live-preview channel.

const _debounce = Duration(milliseconds: 60);
const _dragTick = Duration(milliseconds: 16);

void main() {
  group('WriteCoalescer (throttle)', () {
    test('first schedule fires immediately', () {
      fakeAsync((async) {
        final written = <int>[];
        final coalescer = WriteCoalescer(
          onWrite: (payload) async => written.add(payload[0]),
          debounce: _debounce,
        );

        coalescer.schedule(Uint8List.fromList([1]));
        async.elapse(const Duration(milliseconds: 1));

        expect(written, [1]);
        coalescer.dispose();
      });
    });

    test('schedules within window coalesce to the latest trailing payload',
        () {
      fakeAsync((async) {
        final written = <int>[];
        final coalescer = WriteCoalescer(
          onWrite: (payload) async => written.add(payload[0]),
          debounce: _debounce,
        );

        coalescer.schedule(Uint8List.fromList([1]));
        async.elapse(const Duration(milliseconds: 10));
        coalescer.schedule(Uint8List.fromList([2]));
        async.elapse(const Duration(milliseconds: 10));
        coalescer.schedule(Uint8List.fromList([3]));

        async.elapse(_debounce);
        coalescer.dispose();

        expect(written, [1, 3], reason: '#1 leading, #2 dropped, #3 trailing');
      });
    });

    test('pacing is timer-driven, not ACK-driven', () {
      // Even when each onWrite takes 4x the debounce window to resolve, the
      // throttle fires writes at debounce-spaced intervals. This is the
      // smoothness invariant — a hung GATT write on another characteristic
      // cannot starve the live-preview channel.
      fakeAsync((async) {
        const writeLatency = Duration(milliseconds: 240);
        final fireTimes = <int>[];
        final coalescer = WriteCoalescer(
          onWrite: (payload) {
            fireTimes.add(async.elapsed.inMilliseconds);
            return Future<void>.delayed(writeLatency);
          },
          debounce: _debounce,
        );

        const dragMs = 600;
        final dragTicks = dragMs ~/ _dragTick.inMilliseconds;
        for (var i = 0; i < dragTicks; i++) {
          coalescer.schedule(Uint8List.fromList([i & 0xff]));
          async.elapse(_dragTick);
        }
        async.elapse(const Duration(seconds: 2));
        coalescer.dispose();

        // Expect ~one fire per debounce window during the drag (±1).
        final expectedFires = dragMs ~/ _debounce.inMilliseconds;
        expect(
          fireTimes.length,
          inInclusiveRange(expectedFires, expectedFires + 2),
          reason: 'throttle should fire on the timer schedule, '
              'independent of write latency',
        );
      });
    });

    test('after the window closes, a fresh schedule re-enters leading edge',
        () {
      fakeAsync((async) {
        final written = <int>[];
        final coalescer = WriteCoalescer(
          onWrite: (payload) async => written.add(payload[0]),
          debounce: _debounce,
        );

        coalescer.schedule(Uint8List.fromList([1]));
        async.elapse(_debounce + const Duration(milliseconds: 20));
        // Window has elapsed with no trailing payload → next schedule is
        // a fresh leading edge.
        coalescer.schedule(Uint8List.fromList([2]));
        async.elapse(const Duration(milliseconds: 1));

        expect(written, [1, 2]);
        coalescer.dispose();
      });
    });

    test('schedule() after dispose() is a no-op', () {
      fakeAsync((async) {
        final written = <int>[];
        final coalescer = WriteCoalescer(
          onWrite: (payload) async => written.add(payload[0]),
          debounce: _debounce,
        );
        coalescer.dispose();
        coalescer.schedule(Uint8List.fromList([42]));
        async.elapse(const Duration(seconds: 1));
        expect(written, isEmpty);
      });
    });
  });

  group('KeyedWriteCoalescer (throttle, per key)', () {
    test('writes for different keys do not coalesce', () {
      fakeAsync((async) {
        final written = <String>[];
        final coalescer = KeyedWriteCoalescer<String>(
          onWrite: (key, payload) async => written.add('$key:${payload[0]}'),
          debounce: _debounce,
        );

        coalescer.schedule('a', Uint8List.fromList([1]));
        coalescer.schedule('b', Uint8List.fromList([1]));
        async.elapse(const Duration(milliseconds: 1));

        expect(written, containsAll(['a:1', 'b:1']));
        coalescer.dispose();
      });
    });

    test('within-key schedules collapse to latest trailing', () {
      fakeAsync((async) {
        final written = <String>[];
        final coalescer = KeyedWriteCoalescer<String>(
          onWrite: (key, payload) async => written.add('$key:${payload[0]}'),
          debounce: _debounce,
        );

        coalescer.schedule('a', Uint8List.fromList([1]));
        async.elapse(const Duration(milliseconds: 10));
        coalescer.schedule('a', Uint8List.fromList([2]));
        async.elapse(const Duration(milliseconds: 10));
        coalescer.schedule('a', Uint8List.fromList([3]));
        async.elapse(_debounce);
        coalescer.dispose();

        expect(written, ['a:1', 'a:3']);
      });
    });

    test('schedule() after dispose() is a no-op', () {
      fakeAsync((async) {
        final written = <String>[];
        final coalescer = KeyedWriteCoalescer<String>(
          onWrite: (key, payload) async => written.add('$key:${payload[0]}'),
          debounce: _debounce,
        );
        coalescer.dispose();
        coalescer.schedule('a', Uint8List.fromList([1]));
        async.elapse(const Duration(seconds: 1));
        expect(written, isEmpty);
      });
    });
  });
}
