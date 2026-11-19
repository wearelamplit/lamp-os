// A lamp that ends a working link (it pauses Bluetooth to send firmware) is
// waited out: the app shows it busy and reconnects on its next broadcast,
// with no connect attempts in between. Any other drop runs the reconnect
// ladder.

import 'dart:typed_data';

import 'package:fake_async/fake_async.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:go_router/go_router.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/ble_scanner.dart';
import 'package:lamp_app/core/ble/reaching_lamp_gate.dart';
import 'package:lamp_app/core/routing/router.dart';
import 'package:lamp_app/core/utils/clock.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/control_state.dart';
import 'package:lamp_app/features/firmware/application/firmware_notifier.dart';
import 'package:lamp_app/features/firmware/domain/firmware_state.dart';
import 'package:lamp_app/features/firmware/domain/lsig_footer.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/nearby/application/nearby_lamps_notifier.dart';

const _devId = 'lamp-busy';
const _busyCopy = 'Jacko is busy teaching a lamp new tricks 🎓';

/// Counts connects; can fail them, or have the lamp hang up right after one.
/// A failed connect reports a disconnect of its own, as flutter_blue_plus
/// does for a cancelled or refused connect.
class _Ble extends InMemoryBleClient {
  int connects = 0;
  int failConnects = 0;
  bool hangUpOnConnect = false;

  @override
  Future<void> connect(String deviceId) async {
    connects++;
    if (failConnects > 0) {
      failConnects--;
      await disconnect(deviceId);
      throw StateError('connect failed');
    }
    await super.connect(deviceId);
    if (hangUpOnConnect) {
      hangUpOnConnect = false;
      simulateLampHangUp(deviceId);
    }
  }
}

BleAdvertisement _ad(String id, {bool busy = false}) => BleAdvertisement(
      id: id,
      name: id,
      serviceUuids: const [],
      baseRgb: 0,
      shadeRgb: 0,
      rssi: -60,
      isMesh: true,
      configured: true,
      otaDistributing: busy,
    );

class _DrivenFirmware extends FirmwareNotifier {
  @override
  FirmwareState build(String deviceId) => const FirmwareIdle();
  void emit(FirmwareState s) => state = s;
}

final _footer = LsigFooter(
  channel: 'standard-beta',
  version: 0x010301,
  signedRegionLen: 0,
  signature: Uint8List(64),
);

class _Rig {
  _Rig(this.async) {
    seedControlBle(ble, deviceId: _devId, name: 'jacko');
    c = ProviderContainer(overrides: [
      bleClientProvider.overrideWithValue(ble),
      bleScannerProvider.overrideWithValue(scanner),
      clockProvider.overrideWithValue(() => now),
      firmwareNotifierProvider(_devId).overrideWith(() => firmware),
    ]);
    c.read(inventoryNotifierProvider.future);
    async.flushMicrotasks();
    c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
          id: _devId,
          name: 'jacko',
          controlPassword: '',
        ));
    async.flushMicrotasks();
    sub = c.listen(controlNotifierProvider(_devId), (_, _) {});
    elapse(const Duration(seconds: 1));
  }

  final FakeAsync async;
  final ble = _Ble();
  final scanner = FakeBleScanner();
  final firmware = _DrivenFirmware();
  var now = DateTime.fromMillisecondsSinceEpoch(1700000000000);
  late final ProviderContainer c;
  late final ProviderSubscription<Object?> sub;

  ControlState get state => c.read(controlNotifierProvider(_devId)).value!;

  void elapse(Duration d) {
    now = now.add(d);
    async.elapse(d);
  }

  void close() {
    sub.close();
    c.dispose();
    async.flushTimers();
  }
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('a lamp that hangs up is waited out, then reconnected on its broadcast',
      () {
    fakeAsync((async) {
      final rig = _Rig(async);
      expect(rig.state.connected, isTrue);
      final connectsBefore = rig.ble.connects;

      rig.ble.simulateLampHangUp(_devId);
      rig.elapse(const Duration(seconds: 1));
      expect(rig.state.connected, isFalse);
      expect(rig.state.lampHungUp, isTrue);

      rig.elapse(const Duration(minutes: 2));
      expect(rig.ble.connects, connectsBefore,
          reason: 'no connect while the lamp is away');
      expect(rig.state.reconnectAttempt, 0);

      rig.scanner.emit(_ad(_devId));
      rig.elapse(const Duration(seconds: 2));
      expect(rig.ble.connects, connectsBefore + 1);
      expect(rig.state.connected, isTrue);
      expect(rig.state.lampHungUp, isFalse);
      rig.close();
    });
  });

  test('a broadcast still carrying the busy bit does not reconnect', () {
    fakeAsync((async) {
      final rig = _Rig(async);
      final connectsBefore = rig.ble.connects;
      rig.ble.simulateLampHangUp(_devId);
      rig.elapse(const Duration(seconds: 1));

      rig.scanner.emit(_ad(_devId, busy: true));
      rig.elapse(const Duration(seconds: 30));
      expect(rig.ble.connects, connectsBefore);
      expect(rig.state.lampHungUp, isTrue);
      rig.close();
    });
  });

  test('a sighting from before the hang-up does not reconnect', () {
    fakeAsync((async) {
      final rig = _Rig(async);
      // The picker underneath keeps the scan alive across the session.
      final nearby = rig.c.listen(nearbyLampsNotifierProvider, (_, _) {});
      rig.scanner.emit(_ad(_devId));
      rig.elapse(const Duration(seconds: 2));
      final connectsBefore = rig.ble.connects;

      rig.ble.simulateLampHangUp(_devId);
      rig.elapse(const Duration(seconds: 1));
      rig.scanner.emit(_ad('some-other-lamp'));
      rig.elapse(const Duration(seconds: 5));
      expect(rig.ble.connects, connectsBefore);
      expect(rig.state.lampHungUp, isTrue);
      nearby.close();
      rig.close();
    });
  });

  test('a lamp that hangs up again on the way back in is waited out again',
      () {
    fakeAsync((async) {
      final rig = _Rig(async);
      rig.ble.simulateLampHangUp(_devId);
      rig.elapse(const Duration(seconds: 1));
      final connectsBefore = rig.ble.connects;

      rig.ble.hangUpOnConnect = true;
      rig.scanner.emit(_ad(_devId));
      rig.elapse(const Duration(seconds: 2));
      expect(rig.ble.connects, connectsBefore + 1);
      expect(rig.state.connected, isFalse);
      expect(rig.state.lampHungUp, isTrue);

      rig.elapse(const Duration(minutes: 2));
      expect(rig.ble.connects, connectsBefore + 1);
      rig.close();
    });
  });

  test('a failed connect after the lamp is back falls to the reconnect ladder',
      () {
    fakeAsync((async) {
      final rig = _Rig(async);
      rig.ble.simulateLampHangUp(_devId);
      rig.elapse(const Duration(seconds: 1));
      final connectsBefore = rig.ble.connects;

      rig.ble.failConnects = 1;
      rig.scanner.emit(_ad(_devId));
      async.flushMicrotasks();
      expect(rig.ble.connects, connectsBefore + 1);
      expect(rig.state.lampHungUp, isFalse,
          reason: 'the ladder retrying is not the lamp being busy');

      rig.elapse(const Duration(seconds: 5));
      expect(rig.ble.connects, greaterThan(connectsBefore + 1),
          reason: 'the ladder retries without waiting for a broadcast');
      expect(rig.state.connected, isTrue);
      rig.close();
    });
  });

  test('any other drop retries at once, as before', () {
    fakeAsync((async) {
      final rig = _Rig(async);
      final connectsBefore = rig.ble.connects;

      rig.ble.disconnect(_devId);
      async.flushMicrotasks();
      expect(rig.state.lampHungUp, isFalse);

      rig.elapse(const Duration(seconds: 2));
      expect(rig.ble.connects, greaterThan(connectsBefore));
      expect(rig.state.connected, isTrue);
      rig.close();
    });
  });

  test('a hang-up during the app\'s own firmware push runs the ladder', () {
    fakeAsync((async) {
      final rig = _Rig(async);
      rig.firmware.emit(
          FirmwareStreaming(footer: _footer, chunksSent: 3, totalChunks: 10));
      final connectsBefore = rig.ble.connects;

      rig.ble.simulateLampHangUp(_devId);
      async.flushMicrotasks();
      expect(rig.state.lampHungUp, isFalse);
      rig.elapse(const Duration(seconds: 2));
      expect(rig.ble.connects, greaterThan(connectsBefore));
      rig.close();
    });
  });

  test('a hang-up while a reboot-save reconnects leaves no wait behind', () {
    fakeAsync((async) {
      final rig = _Rig(async);
      final nearby = rig.c.listen(nearbyLampsNotifierProvider, (_, _) {});
      rig.c
          .read(controlNotifierProvider(_devId).notifier)
          .setLampPassword('new-secret');
      async.flushMicrotasks();
      rig.ble.simulateLampHangUp(_devId);
      rig.elapse(const Duration(seconds: 5));
      expect(rig.c.read(controlNotifierProvider(_devId)).value?.connected,
          isTrue, reason: 'the reload brought the link back');
      final connectsAfterReload = rig.ble.connects;

      rig.scanner.emit(_ad(_devId));
      rig.elapse(const Duration(seconds: 2));
      expect(rig.ble.connects, connectsAfterReload,
          reason: 'nothing was waiting on the lamp to broadcast');
      nearby.close();
      rig.close();
    });
  });

  testWidgets('the screen says the lamp is busy after it hangs up',
      (tester) async {
    final ble = _Ble();
    await seedControlBle(ble, deviceId: _devId, name: 'jacko');
    final router = GoRouter(
      initialLocation: '/lamp/$_devId/control',
      routes: [
        GoRoute(
          path: '/lamp/:id/control',
          builder: (_, _) => const Scaffold(body: Text('lamp screen')),
        ),
      ],
    );
    final container = ProviderContainer(overrides: [
      bleClientProvider.overrideWithValue(ble),
      bleScannerProvider.overrideWithValue(FakeBleScanner()),
      appRouterProvider.overrideWithValue(router),
    ]);
    addTearDown(container.dispose);
    await container.read(inventoryNotifierProvider.future);
    await container.read(inventoryNotifierProvider.notifier).add(
        const InventoryLamp(id: _devId, name: 'jacko', controlPassword: ''));

    await tester.pumpWidget(UncontrolledProviderScope(
      container: container,
      child: MaterialApp.router(
        routerConfig: router,
        builder: (context, child) =>
            ReachingLampGate(child: child ?? const SizedBox.shrink()),
      ),
    ));
    await tester.runAsync(
        () => container.read(controlNotifierProvider(_devId).future));
    await tester.pumpAndSettle();
    expect(find.text(_busyCopy), findsNothing);

    ble.simulateLampHangUp(_devId);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    expect(find.text(_busyCopy), findsOneWidget);
    expect(find.text('← pick another lamp'), findsOneWidget);

    await tester.pumpWidget(const SizedBox.shrink());
  });
}
