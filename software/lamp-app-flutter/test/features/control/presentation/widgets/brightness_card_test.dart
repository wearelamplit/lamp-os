import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/lamp_card.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/presentation/widgets/brightness_card.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../_support/seed.dart';

const _devId = 'aa:bb:cc:dd:ee:ff';

Future<ProviderContainer> _buildContainer({int brightness = 42}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId, brightness: brightness);
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'jacko',
        controlPassword: 'secret',
      ));
  await c.read(controlNotifierProvider(_devId).future);
  return c;
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        theme: appTheme,
        home: const Scaffold(
          body: BrightnessCard(lampId: _devId),
        ),
      ),
    );

void main() {
  testWidgets('renders inside LampCard with flanking brightness icons',
      (tester) async {
    final c = await _buildContainer(brightness: 42);
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await tester.pumpAndSettle();

    expect(find.byType(LampCard), findsOneWidget);
    expect(find.byIcon(Icons.brightness_low), findsOneWidget);
    expect(find.byIcon(Icons.brightness_high), findsOneWidget);
    expect(find.byType(Slider), findsOneWidget);
  });

  testWidgets('drag updates state via onChanged and onChangeEnd',
      (tester) async {
    final c = await _buildContainer(brightness: 50);
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await tester.pumpAndSettle();

    final slider = find.byType(Slider);
    await tester.drag(slider, const Offset(50, 0));
    // Pump past the 500ms commit debounce so no pending timers remain.
    await tester.pump(const Duration(milliseconds: 600));

    // onChanged fires during drag → state diverges from initial 50
    final brightness =
        c.read(controlNotifierProvider(_devId)).value!.lamp.brightness;
    expect(brightness, isNot(50));
  });
}
