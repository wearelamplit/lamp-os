import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import '../../_support/in_memory_ble_client.dart';

void main() {
  late InMemoryBleClient ble;

  setUp(() => ble = InMemoryBleClient());

  test('write then read returns the same bytes', () async {
    await ble.connect('dev1');
    await ble.write('dev1', 'svc', 'chr', Uint8List.fromList([1, 2, 3]));
    final read = await ble.read('dev1', 'svc', 'chr');
    expect(read, Uint8List.fromList([1, 2, 3]));
  });

  test('reading a missing characteristic throws BleNotFound', () async {
    await ble.connect('dev1');
    expect(
      () => ble.read('dev1', 'svc', 'missing'),
      throwsA(isA<BleNotFound>()),
    );
  });

  test('subscribe streams simulated notifications', () async {
    // Production semantics: real BLE peripherals
    // DON'T echo our own writes to subscribers — only explicit lamp-
    // pushed notifications fire. The fake honors that contract. Tests
    // that want to observe pushes use simulateNotify().
    await ble.connect('dev1');
    final events = <Uint8List>[];
    final sub = ble.subscribe('dev1', 'svc', 'chr').listen(events.add);
    ble.simulateNotify('dev1', 'svc', 'chr', Uint8List.fromList([7]));
    ble.simulateNotify('dev1', 'svc', 'chr', Uint8List.fromList([8]));
    await Future<void>.delayed(Duration.zero);
    await sub.cancel();
    expect(events.map((b) => b.first).toList(), [7, 8]);
  });

  test('writes do NOT echo to subscribers', () async {
    await ble.connect('dev1');
    final events = <Uint8List>[];
    final sub = ble.subscribe('dev1', 'svc', 'chr').listen(events.add);
    await ble.write('dev1', 'svc', 'chr', Uint8List.fromList([42]));
    await Future<void>.delayed(Duration.zero);
    await sub.cancel();
    expect(events, isEmpty);
  });

  test('scheduleEncryptionFailure throws insufficientEncryption once', () async {
    await ble.connect('dev1');
    ble.scheduleEncryptionFailure('dev1', 'svc', 'enc');
    expect(
      () => ble.write('dev1', 'svc', 'enc', Uint8List.fromList([0])),
      throwsA(isA<BleEncryptionRequired>()),
    );
    // Second write succeeds.
    await ble.write('dev1', 'svc', 'enc', Uint8List.fromList([1]));
    expect(await ble.read('dev1', 'svc', 'enc'), Uint8List.fromList([1]));
  });

  test('InMemoryBleClient tracks connect/disconnect', () async {
    final ble = InMemoryBleClient();
    expect(ble.isConnected('dev1'), isFalse);
    await ble.connect('dev1');
    expect(ble.isConnected('dev1'), isTrue);
    await ble.disconnect('dev1');
    expect(ble.isConnected('dev1'), isFalse);
  });

  test('write throws BleNotConnected on a disconnected device', () async {
    final ble = InMemoryBleClient();
    expect(
      () => ble.write('dev1', 'svc', 'chr', Uint8List.fromList([1])),
      throwsA(isA<BleNotConnected>()),
    );
  });

  test('write succeeds after connect', () async {
    final ble = InMemoryBleClient();
    await ble.connect('dev1');
    await ble.write('dev1', 'svc', 'chr', Uint8List.fromList([1]));
    expect(await ble.read('dev1', 'svc', 'chr'), Uint8List.fromList([1]));
  });

  group('isBleDisconnectError', () {
    test('recognises BleDisconnectedException', () {
      expect(
        isBleDisconnectError(const BleDisconnectedException('dev1')),
        isTrue,
      );
    });

    test('recognises BleNotConnected', () {
      expect(
        isBleDisconnectError(const BleNotConnected('dev1')),
        isTrue,
      );
    });

    test('recognises fbp-style "device is disconnected" message strings', () {
      // Belt-and-suspenders for transient signals that fbp emits as
      // bare exceptions (some platform error paths). Pinning ensures we
      // don't regress when fbp upgrades reword the message.
      expect(
        isBleDisconnectError(Exception('device is disconnected')),
        isTrue,
      );
      expect(
        isBleDisconnectError(Exception('device is not connected')),
        isTrue,
      );
    });

    test('rejects unrelated errors', () {
      expect(isBleDisconnectError(Exception('discoverservices failed')),
          isFalse);
      expect(isBleDisconnectError(Exception('timeout')), isFalse);
      expect(isBleDisconnectError(const FormatException('bad bytes')),
          isFalse);
    });
  });

  group('InMemoryBleClient.watchConnected', () {
    test('emits the current state immediately on subscription', () async {
      final ble = InMemoryBleClient();
      await ble.connect('dev1');
      expect(await ble.watchConnected('dev1').first, isTrue);
    });

    test('emits true on connect after a disconnected start', () async {
      final ble = InMemoryBleClient();
      final events = <bool>[];
      final sub = ble.watchConnected('dev1').listen(events.add);
      // Initial seed: false (never connected)
      await Future<void>.delayed(Duration.zero);
      await ble.connect('dev1');
      await Future<void>.delayed(Duration.zero);
      await sub.cancel();
      expect(events, [false, true]);
    });

    test('emits false on disconnect, true on reconnect', () async {
      final ble = InMemoryBleClient();
      await ble.connect('dev1');
      final events = <bool>[];
      final sub = ble.watchConnected('dev1').listen(events.add);
      await Future<void>.delayed(Duration.zero);
      await ble.disconnect('dev1');
      await Future<void>.delayed(Duration.zero);
      await ble.connect('dev1');
      await Future<void>.delayed(Duration.zero);
      await sub.cancel();
      expect(events, [true, false, true]);
    });
  });
}
