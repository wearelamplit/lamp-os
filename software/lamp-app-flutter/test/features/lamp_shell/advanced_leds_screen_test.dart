import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/advanced_leds_screen.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-x';

Future<ProviderContainer> _makeContainer() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId, baseKnockoutJson: '[100,50,0]');
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'jacko',
        controlPassword: 'secret',
      ));
  return c;
}

Future<void> _pumpToData(WidgetTester tester) async {
  for (var i = 0; i < 30; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text('Shade strip').evaluate().isNotEmpty) return;
  }
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      // Plain MaterialApp: these tests assert row presence + order, they
      // don't tap the knockout row (which would need a Router for push).
      child: const MaterialApp(home: AdvancedLedsScreen(lampId: _devId)),
    );

void main() {
  testWidgets('Shade strip section renders above Base strip', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    expect(find.text('Shade strip'), findsOneWidget);
    expect(find.text('Base strip'), findsOneWidget);
    expect(
      tester.getCenter(find.text('Shade strip')).dy,
      lessThan(tester.getCenter(find.text('Base strip')).dy),
    );
  });

  testWidgets('Per-pixel knockout entry is nested in LED setup', (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    expect(find.text('Per-pixel knockout'), findsOneWidget);
  });

  testWidgets('knockout entry sits under the Base strip section',
      (tester) async {
    final c = await _makeContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(_wrap(c));
    await _pumpToData(tester);

    // Knockout masks base pixels only, so its entry belongs below the
    // Base strip header, not the Shade one.
    expect(
      tester.getCenter(find.text('Base strip')).dy,
      lessThan(tester.getCenter(find.text('Per-pixel knockout')).dy),
    );
  });
}
