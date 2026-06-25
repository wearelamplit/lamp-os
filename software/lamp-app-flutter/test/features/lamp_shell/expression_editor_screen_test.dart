import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/presentation/expression_editor_screen.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-x';

/// Build a container with BLE seeded and inventory populated.
/// The controlNotifier is NOT pre-primed — the widget's watch primes it.
/// Pre-priming closes a temporary subscription inside FakeAsync, which
/// schedules Riverpod's 0-ms dispose timer and fails with "pending timer".
Future<ProviderContainer> _withEmptyState() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId, name: 'test');
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
/// ConnectingView animates infinitely so pumpAndSettle never converges.
Future<void> _pumpToData(WidgetTester tester, String sentinel) async {
  for (var i = 0; i < 30; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text(sentinel).evaluate().isNotEmpty) return;
  }
}

void main() {
  testWidgets('new-expression editor shows the picked type + target header',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
          lampId: _devId,
          typeKey: 'breathing',
          targetKey: 3,
        ),
      ),
    ));
    await _pumpToData(tester, 'Breathing');
    // Header carries the resolved type name; the target row below the
    // header shows all three pills with the active target highlighted.
    expect(find.text('Breathing'), findsOneWidget);
    expect(find.text('Shade'), findsOneWidget);
    expect(find.text('Base'), findsOneWidget);
    expect(find.text('Both'), findsOneWidget);

    // Add button (new expression — existing entries say "Save") may be
    // below the fold; scroll to reveal it.
    await tester.dragUntilVisible(
      find.text('Add'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('Add'), findsOneWidget);
  });

  testWidgets('Save adds the entry to ControlState.expressions',
      (tester) async {
    final ble = InMemoryBleClient();
    SharedPreferences.setMockInitialValues({});
    await seedControlBle(ble, deviceId: _devId, name: 'test');
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
          id: _devId,
          name: 'jacko',
          controlPassword: 'secret',
        ));

    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
          lampId: _devId,
          typeKey: 'breathing',
          targetKey: 3,
        ),
      ),
    ));
    await _pumpToData(tester, 'Breathing');
    await tester.dragUntilVisible(
      find.text('Add'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    await tester.tap(find.text('Add'));
    // Drain microtasks for the async upsert.
    for (var i = 0; i < 30; i++) {
      await tester.pump(const Duration(milliseconds: 16));
      final list = c
          .read(controlNotifierProvider(_devId))
          .value!
          .expressions
          .expressions;
      if (list.isNotEmpty) break;
    }
    expect(
      c
          .read(controlNotifierProvider(_devId))
          .value!
          .expressions
          .expressions,
      hasLength(1),
    );
  });

  testWidgets('predictability slider labels read "less" and "more"',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        // glitchy is a trigger-based expression that surfaces the
        // FrequencySpread widget. Breathing is continuous and hides it.
        home: ExpressionEditorScreen(
            lampId: _devId, typeKey: 'glitchy', targetKey: 3),
      ),
    ));
    await _pumpToData(tester, 'Glitchy');
    await tester.dragUntilVisible(
      find.text('less'), find.byType(ListView), const Offset(0, -200));
    expect(find.text('less'), findsOneWidget);
    expect(find.text('more'), findsOneWidget);
    expect(find.text('exact'), findsNothing);
    expect(find.text('varied'), findsNothing);
  });

  testWidgets('frequency slider labels read "rare" and "often"',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        // FrequencySpread is only rendered for trigger-based expressions.
        home: ExpressionEditorScreen(
            lampId: _devId, typeKey: 'glitchy', targetKey: 3),
      ),
    ));
    await _pumpToData(tester, 'Glitchy');
    await tester.dragUntilVisible(
      find.text('rare'), find.byType(ListView), const Offset(0, -200));
    expect(find.text('rare'), findsOneWidget);
    expect(find.text('often'), findsOneWidget);
  });

  testWidgets('frequency thumb does not move when predictability changes',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        // FrequencySpread is only rendered for trigger-based expressions.
        home: ExpressionEditorScreen(
            lampId: _devId, typeKey: 'glitchy', targetKey: 3),
      ),
    ));
    await _pumpToData(tester, 'Glitchy');
    await tester.dragUntilVisible(
      find.text('Predictability'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    // Find the two 0..1 sliders (freq + spread) — filtering out any other
    // sliders (e.g. the speed/duration slider in ExpressionParamsPanel).
    final normSliders = find.byWidgetPredicate(
        (w) => w is Slider && w.min == 0.0 && w.max == 1.0);
    expect(normSliders, findsNWidgets(2));
    final freqBefore = tester.widget<Slider>(normSliders.at(0)).value;
    // Drag the predictability slider (second 0..1 one) to the right.
    await tester.drag(normSliders.at(1), const Offset(120, 0));
    await tester.pump();
    final freqAfter = tester.widget<Slider>(normSliders.at(0)).value;
    expect(freqAfter, freqBefore,
        reason: 'Frequency thumb must not move when predictability drags');
  });
}
