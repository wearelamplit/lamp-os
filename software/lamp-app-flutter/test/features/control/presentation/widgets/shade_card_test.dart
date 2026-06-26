import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/presentation/widgets/shade_card.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../_support/seed.dart';

const _devId = 'aa:bb:cc:dd:ee:ff';

Future<ProviderContainer> _buildContainer() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble,
      deviceId: _devId,
      shadeColorsJson: '["#300783FF"]',
      shadeBpp: 4);
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

void main() {
  testWidgets('renders the "Shade" label and no hex string', (tester) async {
    final c = await _buildContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(
          body: ShadeCard(lampId: _devId),
        ),
      ),
    ));
    await tester.pumpAndSettle();
    expect(find.text('Shade'), findsOneWidget);
    expect(find.textContaining('#'), findsNothing);
    expect(find.textContaining('300783'), findsNothing);
  });

  testWidgets('renders a gradient preview swatch (matches BaseCard)',
      (tester) async {
    final c = await _buildContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(
          body: ShadeCard(lampId: _devId),
        ),
      ),
    ));
    await tester.pumpAndSettle();
    // The shade preview is a Container with a LinearGradient decoration —
    // shade is now a gradient (parity with base), not a single color, so
    // the previous LampColorSwatch widget was replaced with the same
    // gradient pattern BaseCard uses.
    final containers = tester.widgetList<Container>(find.byType(Container));
    final hasGradientContainer = containers.any((c) {
      final dec = c.decoration;
      return dec is BoxDecoration && dec.gradient is LinearGradient;
    });
    expect(hasGradientContainer, isTrue);
  });
}
