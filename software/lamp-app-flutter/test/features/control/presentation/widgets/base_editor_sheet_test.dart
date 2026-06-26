import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/presentation/widgets/base_editor_sheet.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../_support/seed.dart';

// Test strategy: BaseEditorSheet is now a ConsumerWidget that reads live state
// from controlNotifierProvider. Tests create a ProviderContainer, seed
// inventory, then wrap with UncontrolledProviderScope — so the provider is
// ready before the widget tree starts watching it.

const _devId = 'lamp-x';

Future<void> _seedBle(InMemoryBleClient ble, {int stopCount = 2}) {
  String hex(int v) => v.toRadixString(16).padLeft(6, '0').toUpperCase();
  final colors = List.generate(
    stopCount,
    (i) => '"#${hex(((i + 1) * 0x300783) & 0xFFFFFF)}FF"',
  ).join(',');
  return seedControlBle(
    ble,
    deviceId: _devId,
    name: 'test',
    baseColorsJson: '[$colors]',
  );
}

/// Build a container, seed the inventory lamp, and await the control notifier
/// so state is ready before the widget tree renders.
Future<ProviderContainer> _buildContainer(
    InMemoryBleClient ble, {
    int stopCount = 2,
  }) async {
  await _seedBle(ble, stopCount: stopCount);
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'test',
        controlPassword: '',
      ));
  // Prime the notifier so it's in data state when the widget first builds.
  await c.read(controlNotifierProvider(_devId).future);
  return c;
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  testWidgets('renders SizedBox while notifier is in loading state',
      (tester) async {
    // No BLE data seeded and no inventory lamp — notifier stays loading.
    final ble = InMemoryBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: BaseEditorSheet(lampId: _devId)),
      ),
    ));
    // Provider stays in loading — widget renders SizedBox.shrink().
    expect(find.text('+ Add stop'), findsNothing);
    expect(find.text('Base colors'), findsNothing);
  });

  testWidgets('renders one row per stop and add-stop CTA when < 6 stops',
      (tester) async {
    final ble = InMemoryBleClient();
    final c = await _buildContainer(ble, stopCount: 2);
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: BaseEditorSheet(lampId: _devId)),
      ),
    ));
    await tester.pumpAndSettle();

    expect(find.text('Base colors'), findsOneWidget);
    expect(find.text('+ Add stop'), findsOneWidget);
    expect(find.byIcon(Icons.drag_indicator), findsNWidgets(2));
  });

  testWidgets('Save button pops the route', (tester) async {
    final ble = InMemoryBleClient();
    final c = await _buildContainer(ble, stopCount: 2);
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Builder(
          builder: (ctx) => Scaffold(
            body: TextButton(
              onPressed: () => showBaseEditorSheet(ctx, lampId: _devId),
              child: const Text('open'),
            ),
          ),
        ),
      ),
    ));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    expect(find.text('Base colors'), findsOneWidget);

    // Save button closes the sheet without reverting (the X-close was
    // replaced by an explicit Cancel/Save row when the unified save
    // model added per-screen atomic undo).
    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();
    expect(find.text('Base colors'), findsNothing);
  });

  testWidgets('hides add-stop CTA at 6 stops (cap)', (tester) async {
    final ble = InMemoryBleClient();
    final c = await _buildContainer(ble, stopCount: 6);
    addTearDown(c.dispose);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: BaseEditorSheet(lampId: _devId)),
      ),
    ));
    await tester.pumpAndSettle();

    expect(find.text('+ Add stop'), findsNothing);
    expect(find.byIcon(Icons.drag_indicator), findsNWidgets(6));
  });
}
