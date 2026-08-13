import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/domain/sections.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-a';

Future<ProviderContainer> _buildContainer() async {
  final ble = InMemoryBleClient();
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
  await c.read(controlNotifierProvider(_devId).future);

  return c;
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('controlNotifier._mutate (via mutateForTest)', () {
    test('applies the transform optimistically', () async {
      final c = await _buildContainer();
      addTearDown(c.dispose);

      final n = c.read(controlNotifierProvider(_devId).notifier);
      final before = c.read(controlNotifierProvider(_devId)).value!;
      const newName = 'mutate-test-name';

      await n.mutateForTest(
        (s) => s.copyWith(
          lamp: LampSection(
            name: newName,
            brightness: s.lamp.brightness,
            advancedEnabled: s.lamp.advancedEnabled,
            webappEnabled: s.lamp.webappEnabled,
            socialMode: s.lamp.socialMode,
            fwVersion: s.lamp.fwVersion,
            fwChannel: s.lamp.fwChannel,
          ),
        ),
        () async {}, // no-op commit
      );

      expect(
        c.read(controlNotifierProvider(_devId)).value!.lamp.name,
        newName,
      );
      // Sanity: original name was different
      expect(before.lamp.name, isNot(newName));
    });

    test('reverts state on commit failure', () async {
      final c = await _buildContainer();
      addTearDown(c.dispose);

      final n = c.read(controlNotifierProvider(_devId).notifier);
      final originalState =
          c.read(controlNotifierProvider(_devId)).value;

      await expectLater(
        () => n.mutateForTest(
          (s) => s.copyWith(
            lamp: LampSection(
              name: 'should-be-reverted',
              brightness: s.lamp.brightness,
              advancedEnabled: s.lamp.advancedEnabled,
              webappEnabled: s.lamp.webappEnabled,
              socialMode: s.lamp.socialMode,
              fwVersion: s.lamp.fwVersion,
              fwChannel: s.lamp.fwChannel,
            ),
          ),
          () async => throw Exception('boom'),
        ),
        throwsA(isA<Exception>()),
      );

      expect(
        c.read(controlNotifierProvider(_devId)).value,
        originalState,
      );
    });
  });
}
