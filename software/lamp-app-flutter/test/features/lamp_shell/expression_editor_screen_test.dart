import 'dart:math' show log, ln10, pow;

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/expression_draft.dart';
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
      find.text('Save'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('Save'), findsOneWidget);
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
      find.text('Save'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    await tester.tap(find.text('Save'));
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

  testWidgets(
      'switching target moves the in-progress draft in place (no reset, no duplicate)',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
          lampId: _devId,
          typeKey: 'breathing',
          targetKey: 1, // Shade
        ),
      ),
    ));
    await _pumpToData(tester, 'Breathing');

    // Simulate in-progress work on the Shade draft.
    c
        .read(expressionDraftProvider(_devId, 'breathing', 1).notifier)
        .update((d) => d.copyWith(intervalMin: 123));
    await tester.pump();

    // Tap a different (untaken) target — should retarget in place, not
    // navigate to a blank editor.
    await tester.tap(find.text('Base'));
    await tester.pump();

    // Work moved onto the Base slot, retargeted…
    final baseDraft = c.read(expressionDraftProvider(_devId, 'breathing', 2));
    expect(baseDraft.target, 2);
    expect(baseDraft.intervalMin, 123,
        reason: 'in-progress edits carry to the new target');

    // …and the old Shade slot was dropped (rebuilds to a fresh default),
    // so the work moved rather than duplicating across both targets.
    final shadeDraft = c.read(expressionDraftProvider(_devId, 'breathing', 1));
    expect(shadeDraft.intervalMin, 60,
        reason: 'old target slot reset — work moved, not duplicated');
  });

  testWidgets(
      'moving an existing expression to a new target removes the old row (no duplicate)',
      (tester) async {
    final ble = InMemoryBleClient();
    SharedPreferences.setMockInitialValues({});
    await seedControlBle(
      ble,
      deviceId: _devId,
      name: 'test',
      expressionsJson:
          '[{"type":"breathing","enabled":true,"colors":[],"intervalMin":123,"intervalMax":900,"target":1}]',
    );
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
          targetKey: 1, // Shade — the existing row
        ),
      ),
    ));
    await _pumpToData(tester, 'Breathing');

    // One existing entry on Shade.
    expect(
      c.read(controlNotifierProvider(_devId)).value!.expressions.expressions,
      hasLength(1),
    );

    // Move it to Base, then Save.
    await tester.tap(find.text('Base'));
    await tester.pump();
    await tester.dragUntilVisible(
        find.text('Save'), find.byType(ListView), const Offset(0, -200));
    await tester.tap(find.text('Save'));
    for (var i = 0; i < 30; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    // Still exactly one entry, now on Base — the Shade row was dropped, not
    // duplicated. (Edit-carry across the switch is covered by the in-place
    // test above; here the keepAlive draft seeds before the section finishes
    // loading, so we only assert the structural move invariant.)
    final exprs =
        c.read(controlNotifierProvider(_devId)).value!.expressions.expressions;
    expect(exprs, hasLength(1), reason: 'move must not duplicate');
    expect(exprs.single.target, 2);
  });

  testWidgets('interval range slider renders for trigger-based expressions',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        // glitchy is trigger-based; breathing hides the interval slider.
        home: ExpressionEditorScreen(
            lampId: _devId, typeKey: 'glitchy', targetKey: 3),
      ),
    ));
    await _pumpToData(tester, 'Glitchy');
    await tester.dragUntilVisible(
      find.text('TRIGGER INTERVAL'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('TRIGGER INTERVAL'), findsOneWidget);
    // Slider now lives in log10 space: min≈1.0, max≈3.556.
    final rangeSlider = find.byWidgetPredicate((w) =>
        w is RangeSlider &&
        (w.min - 1.0).abs() < 0.01 &&
        (w.max - 3.556).abs() < 0.01);
    expect(rangeSlider, findsOneWidget);
  });

  testWidgets('back with unsaved edits prompts to discard', (tester) async {
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

    // Dirty the draft by retargeting, then hit the AppBar back arrow.
    await tester.tap(find.text('Base'));
    await tester.pump();
    await tester.tap(find.byIcon(Icons.arrow_back));
    await tester.pump();
    await tester.pump();

    // The shared discard-guard dialog (base/shade editors) appears.
    expect(find.text('Discard changes?'), findsOneWidget);
  });

  test('log-scale helpers round-trip common intervals and expand the low band',
      () {
    // Mirror the call-site helpers for isolated verification.
    double secToPos(int sec) => log(sec) / ln10;
    int posToSec(double pos) =>
        pow(10, pos).round().clamp(10, 3600) as int;

    // Round-trips within rounding error.
    expect(posToSec(secToPos(10)), 10);
    expect(posToSec(secToPos(60)), 60);
    expect(posToSec(secToPos(3600)), 3600);

    // 60 s sits at a much higher track position in log scale than it would
    // occupy on a linear 10–3600 track mapped to the same [1.0, 3.556] range,
    // demonstrating the common 10–60 s band gets proportionally more track.
    const trackMin = 1.0;   // log10(10)
    const trackMax = 3.556; // log10(3600) ≈
    final logPos60 = secToPos(60); // ≈ 1.778
    final linearPos60 =
        trackMin + (60 - 10) / (3600 - 10) * (trackMax - trackMin); // ≈ 1.036
    expect(logPos60, greaterThan(linearPos60),
        reason:
            '60 s occupies more track in log scale; common band is not squished');
  });
}
