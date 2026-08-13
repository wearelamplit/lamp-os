import 'package:fake_async/fake_async.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-a';

Future<ProviderContainer> _makeContainer(InMemoryBleClient ble) async {
  SharedPreferences.setMockInitialValues({});
  await seedControlBle(ble, deviceId: _devId, brightness: 50);

  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );

  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'test-lamp',
        controlPassword: '',
      ));

  // Keep a listener alive so the provider stays connected.
  c.listen(controlNotifierProvider(_devId), (_, _) {});

  // Await build() completion.
  await c.read(controlNotifierProvider(_devId).future);

  return c;
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('commit debounce', () {
    test('fires commit 500ms after last schedule', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        late ProviderContainer container;

        // Set up the container synchronously inside fakeAsync.
        _makeContainer(ble).then((c) => container = c);
        async.flushMicrotasks();

        final notifier =
            container.read(controlNotifierProvider(_devId).notifier);

        notifier.scheduleCommitDebouncedForTest();

        async.elapse(const Duration(milliseconds: 400));
        async.flushMicrotasks();
        expect(ble.writesTo(_devId, BleUuids.commit).length, 0,
            reason: 'should not fire before 500ms');

        async.elapse(const Duration(milliseconds: 100));
        async.flushMicrotasks();
        expect(ble.writesTo(_devId, BleUuids.commit).length, 1,
            reason: 'should fire exactly once at 500ms');

        container.dispose();
      });
    });

    test('rapid schedules collapse to one commit', () {
      fakeAsync((async) {
        final ble = InMemoryBleClient();
        late ProviderContainer container;

        _makeContainer(ble).then((c) => container = c);
        async.flushMicrotasks();

        final notifier =
            container.read(controlNotifierProvider(_devId).notifier);

        notifier.scheduleCommitDebouncedForTest();
        async.elapse(const Duration(milliseconds: 100));
        notifier.scheduleCommitDebouncedForTest();
        async.elapse(const Duration(milliseconds: 100));
        notifier.scheduleCommitDebouncedForTest();
        async.elapse(const Duration(milliseconds: 500));
        async.flushMicrotasks();

        expect(ble.writesTo(_devId, BleUuids.commit).length, 1,
            reason: 'three rapid calls should collapse to one commit');

        container.dispose();
      });
    });

    test('dispose flushes pending commit', () async {
      // Real timers — verify the dispose path fires commit synchronously.
      final ble = InMemoryBleClient();
      final container = await _makeContainer(ble);

      final notifier =
          container.read(controlNotifierProvider(_devId).notifier);
      notifier.scheduleCommitDebouncedForTest();

      // Dispose before the 500ms window expires.
      container.dispose();

      // Give the fire-and-forget unawaited commit a tick to land.
      await Future.delayed(const Duration(milliseconds: 50));

      expect(ble.writesTo(_devId, BleUuids.commit).length, 1,
          reason: 'dispose should flush the pending commit immediately');
    });
  });
}
