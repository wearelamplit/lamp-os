import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/base_editor_sheet.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../_support/seed.dart';

// Test strategy: BaseEditorSheet is a ConsumerStatefulWidget that reads live
// state from controlNotifierProvider. Tests create a ProviderContainer, seed
// inventory, then wrap with UncontrolledProviderScope — so the provider is
// ready before the widget tree starts watching it.

const _devId = 'lamp-x';

// Stop 0 color from _seedBle with stopCount=2:
// (1 * 0x300783) & 0xFFFFFF = 0x300783 → r=0x30, g=0x07, b=0x83, w=0xFF
const _baselineColor = LampColor(r: 0x30, g: 0x07, b: 0x83, w: 0xFF);

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

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        theme: appTheme,
        home: Builder(
          builder: (ctx) => Scaffold(
            body: TextButton(
              onPressed: () => showBaseEditorSheet(ctx, lampId: _devId),
              child: const Text('open'),
            ),
          ),
        ),
      ),
    );

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

  // --- Discard-guard tests (mirrors shade_editor_sheet_test.dart) ---

  testWidgets('Cancel reverts base colors and closes', (tester) async {
    final ble = InMemoryBleClient();
    final c = await _buildContainer(ble);
    addTearDown(c.dispose);
    // Keep provider alive so we can read state after the sheet is dismissed.
    final sub = c.listen(controlNotifierProvider(_devId), (_, __) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(c));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    expect(find.byType(BaseEditorSheet), findsOneWidget);

    // Remove the second stop — base.colors goes from 2 → 1.
    await tester.tap(find.byIcon(Icons.close).last);
    await tester.pumpAndSettle();

    await tester.tap(find.text('Cancel'));
    await tester.pumpAndSettle();

    expect(find.byType(BaseEditorSheet), findsNothing);
    final colors = c.read(controlNotifierProvider(_devId)).value!.base.colors;
    expect(colors.length, 2);
    expect(colors.first, _baselineColor);
  });

  testWidgets('Save commits (writes to commit char) and closes',
      (tester) async {
    final ble = InMemoryBleClient();
    final c = await _buildContainer(ble);
    addTearDown(c.dispose);
    final sub = c.listen(controlNotifierProvider(_devId), (_, __) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(c));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // Remove a stop to make an edit.
    await tester.tap(find.byIcon(Icons.close).last);
    await tester.pumpAndSettle();

    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();

    expect(ble.writesTo(_devId, BleUuids.commit), isNotEmpty);
    expect(find.byType(BaseEditorSheet), findsNothing);
    // Edited value (1 stop) is preserved, not reverted.
    final colors = c.read(controlNotifierProvider(_devId)).value!.base.colors;
    expect(colors.length, 1);
  });

  testWidgets('System-back with no changes closes immediately without dialog',
      (tester) async {
    final ble = InMemoryBleClient();
    final c = await _buildContainer(ble);
    addTearDown(c.dispose);
    final sub = c.listen(controlNotifierProvider(_devId), (_, __) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(c));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // No edits — sheet is clean.
    await tester.binding.handlePopRoute();
    await tester.pumpAndSettle();

    expect(find.text('Discard changes?'), findsNothing);
    expect(find.byType(BaseEditorSheet), findsNothing);
  });

  testWidgets(
      'System-back with unsaved color changes shows discard dialog; Discard reverts+closes',
      (tester) async {
    final ble = InMemoryBleClient();
    final c = await _buildContainer(ble);
    addTearDown(c.dispose);
    final sub = c.listen(controlNotifierProvider(_devId), (_, __) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(c));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // Remove a stop to dirty the state.
    await tester.tap(find.byIcon(Icons.close).last);
    await tester.pumpAndSettle();

    // Simulate system back.
    await tester.binding.handlePopRoute();
    await tester.pumpAndSettle();

    expect(find.text('Discard changes?'), findsOneWidget);
    await tester.tap(find.text('Discard'));
    await tester.pumpAndSettle();

    expect(find.byType(BaseEditorSheet), findsNothing);
    final colors = c.read(controlNotifierProvider(_devId)).value!.base.colors;
    expect(colors.length, 2);
    expect(colors.first, _baselineColor);
  });

  testWidgets(
      'Changing only ac (colors unchanged) counts as unsaved; system-back shows discard dialog',
      (tester) async {
    final ble = InMemoryBleClient();
    final c = await _buildContainer(ble); // baseAc=0 (default), 2 stops
    addTearDown(c.dispose);
    final sub = c.listen(controlNotifierProvider(_devId), (_, __) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(c));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // Change only ac — colors stay the same.
    c.read(controlNotifierProvider(_devId).notifier).setBaseAc(1);
    await tester.pump();

    // Simulate system back.
    await tester.binding.handlePopRoute();
    await tester.pumpAndSettle();

    // ac changed (0→1) with colors unchanged → still unsaved → dialog shown.
    expect(find.text('Discard changes?'), findsOneWidget);
  });
}
