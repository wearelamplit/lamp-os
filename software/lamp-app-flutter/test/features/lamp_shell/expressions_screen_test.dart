import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/expressions_screen.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-x';

Future<void> _seed(InMemoryBleClient ble,
        {required String expressionsJson}) =>
    seedControlBle(ble, deviceId: _devId, name: 'test',
        expressionsJson: expressionsJson);

/// Build a container with BLE seeded and inventory populated.
/// The controlNotifier is NOT pre-primed — the widget's watch primes it.
/// Pre-priming closes a temporary subscription inside FakeAsync, which
/// schedules Riverpod's 0-ms dispose timer and fails with "pending timer".
Future<ProviderContainer> _withState(String expressionsJson) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await _seed(ble, expressionsJson: expressionsJson);
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

/// Pump enough frames for InMemoryBleClient async operations to resolve.
Future<void> _pumpToData(WidgetTester tester, String sentinel) async {
  for (var i = 0; i < 30; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text(sentinel).evaluate().isNotEmpty) return;
  }
}

void main() {
  testWidgets('renders empty-state when no expressions', (tester) async {
    final c = await _withState('[]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionsScreen(lampId: _devId),
      ),
    ));
    await _pumpToData(tester, 'No expressions yet');
    expect(find.text('No expressions yet'), findsOneWidget);
  });

  testWidgets('renders one tile per expression', (tester) async {
    final c = await _withState(
        '[{"type":"breathing","enabled":true,"colors":[],'
        '"intervalMin":10,"intervalMax":20,"target":1},'
        '{"type":"glitchy","enabled":false,"colors":[],'
        '"intervalMin":60,"intervalMax":900,"target":3}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionsScreen(lampId: _devId),
      ),
    ));
    await _pumpToData(tester, 'Breathing');
    expect(find.text('Breathing'), findsOneWidget);
    expect(find.text('Glitchy'), findsOneWidget);
  });

  testWidgets('swipe-to-delete shows a confirm dialog; Cancel keeps the tile',
      (tester) async {
    final c = await _withState(
        '[{"type":"breathing","enabled":true,"colors":[],'
        '"intervalMin":10,"intervalMax":20,"target":1}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionsScreen(lampId: _devId),
      ),
    ));
    await _pumpToData(tester, 'Breathing');

    await tester.drag(find.text('Breathing'), const Offset(-500, 0));
    await tester.pump(); // open dialog
    await tester.pump(const Duration(milliseconds: 300));

    expect(find.text('Delete the "breathing" expression?'), findsOneWidget);
    await tester.tap(find.text('Cancel'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 300));

    // Tile is still there
    expect(find.text('Breathing'), findsOneWidget);
  });

  testWidgets('confirm delete removes the entry and surfaces an UNDO snackbar',
      (tester) async {
    final c = await _withState(
        '[{"type":"breathing","enabled":true,"colors":[],'
        '"intervalMin":10,"intervalMax":20,"target":1}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionsScreen(lampId: _devId),
      ),
    ));
    await _pumpToData(tester, 'Breathing');

    await tester.drag(find.text('Breathing'), const Offset(-500, 0));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 300));
    await tester.tap(find.text('Delete'));
    for (var i = 0; i < 30; i++) {
      await tester.pump(const Duration(milliseconds: 16));
      if (find.text('UNDO').evaluate().isNotEmpty) break;
    }

    expect(find.text('Breathing'), findsNothing);
    expect(find.text('UNDO'), findsOneWidget);
    expect(find.textContaining('Removed'), findsOneWidget);

    // Notifier state is now empty — the delete actually landed.
    expect(
      c.read(controlNotifierProvider(_devId))
          .value!.expressions.expressions,
      isEmpty,
    );

    // The UNDO action's restore via notifier.upsertExpression is exercised
    // directly by control_notifier_test.dart; here we only verify the
    // snackbar plumbing.
  });

  testWidgets('toggling the Enabled switch flips ExpressionConfig.enabled',
      (tester) async {
    final c = await _withState(
        '[{"type":"breathing","enabled":true,"colors":[],'
        '"intervalMin":10,"intervalMax":20,"target":1}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionsScreen(lampId: _devId),
      ),
    ));
    await _pumpToData(tester, 'Breathing');

    await tester.tap(find.byType(Switch).first);
    await tester.pump();

    final e = c
        .read(controlNotifierProvider(_devId))
        .value!
        .expressions
        .expressions
        .firstWhere((x) => x.type == 'breathing' && x.target == 1);
    expect(e.enabled, isFalse);
  });

  testWidgets('tile shows titleized name from registry', (tester) async {
    final c = await _withState(
        '[{"type":"breathing","enabled":true,"colors":[],'
        '"intervalMin":10,"intervalMax":20,"target":1}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: ExpressionsScreen(lampId: _devId)),
    ));
    await _pumpToData(tester, 'Breathing');
    expect(find.text('Breathing'), findsOneWidget);
    expect(find.text('breathing'), findsNothing);
  });

  testWidgets('tile shows expression icon in aurora-blue circle',
      (tester) async {
    final c = await _withState(
        '[{"type":"breathing","enabled":true,"colors":[],'
        '"intervalMin":10,"intervalMax":20,"target":1}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: ExpressionsScreen(lampId: _devId)),
    ));
    await _pumpToData(tester, 'Breathing');
    // The breathing meta uses Icons.air.
    expect(find.byIcon(Icons.air), findsOneWidget);
  });

  testWidgets('looped pulse groups as Continuous and hides the play button',
      (tester) async {
    final c = await _withState(
        '[{"type":"pulse","enabled":true,"colors":[],'
        '"intervalMin":60,"intervalMax":900,"target":3,"loop":1}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: ExpressionsScreen(lampId: _devId)),
    ));
    await _pumpToData(tester, 'Pulse');

    expect(find.text('CONTINUOUS'), findsOneWidget);
    expect(find.text('TRIGGERED'), findsNothing);
    expect(find.byIcon(Icons.play_arrow_rounded), findsNothing);
  });

  testWidgets('triggered pulse groups as Triggered and shows the play button',
      (tester) async {
    final c = await _withState(
        '[{"type":"pulse","enabled":true,"colors":[],'
        '"intervalMin":60,"intervalMax":900,"target":3,"loop":0}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: ExpressionsScreen(lampId: _devId)),
    ));
    await _pumpToData(tester, 'Pulse');

    expect(find.text('TRIGGERED'), findsOneWidget);
    expect(find.text('CONTINUOUS'), findsNothing);
    expect(find.byIcon(Icons.play_arrow_rounded), findsOneWidget);
  });

  testWidgets('trigger ▶ disables for the cooldown then re-enables',
      (tester) async {
    // glitchy: duration unit ms, so durationMax=2000 => 2000 ms cooldown.
    final c = await _withState(
        '[{"type":"glitchy","enabled":true,"colors":[],'
        '"intervalMin":60,"intervalMax":900,"target":3,"durationMax":2000}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: ExpressionsScreen(lampId: _devId)),
    ));
    await _pumpToData(tester, 'Glitchy');

    IconButton btn() => tester.widget<IconButton>(
        find.widgetWithIcon(IconButton, Icons.play_arrow_rounded));
    expect(btn().onPressed, isNotNull);

    await tester.tap(find.byIcon(Icons.play_arrow_rounded));
    await tester.pump();
    expect(btn().onPressed, isNull);

    await tester.pump(const Duration(milliseconds: 1999));
    expect(btn().onPressed, isNull);

    await tester.pump(const Duration(milliseconds: 2));
    expect(btn().onPressed, isNotNull);
  });

  testWidgets('trigger ▶ on a duration-less expression uses the 2s fallback',
      (tester) async {
    final c = await _withState(
        '[{"type":"pulse","enabled":true,"colors":[],'
        '"intervalMin":60,"intervalMax":900,"target":3,"loop":0}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: ExpressionsScreen(lampId: _devId)),
    ));
    await _pumpToData(tester, 'Pulse');

    IconButton btn() => tester.widget<IconButton>(
        find.widgetWithIcon(IconButton, Icons.play_arrow_rounded));

    await tester.tap(find.byIcon(Icons.play_arrow_rounded));
    await tester.pump();
    expect(btn().onPressed, isNull);

    await tester.pump(const Duration(milliseconds: 1999));
    expect(btn().onPressed, isNull);

    await tester.pump(const Duration(milliseconds: 2));
    expect(btn().onPressed, isNotNull);
  });

  testWidgets('tile does not show raw interval seconds', (tester) async {
    final c = await _withState(
        '[{"type":"breathing","enabled":true,"colors":[],'
        '"intervalMin":60,"intervalMax":900,"target":1}]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: ExpressionsScreen(lampId: _devId)),
    ));
    await _pumpToData(tester, 'Breathing');
    expect(find.textContaining('60-900'), findsNothing);
  });
}
