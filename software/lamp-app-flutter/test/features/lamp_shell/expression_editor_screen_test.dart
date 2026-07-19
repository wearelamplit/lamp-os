import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/expression_draft.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_blocks_bar.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/lamp_shell/domain/expression_catalog.dart';
import 'package:lamp_app/features/lamp_shell/presentation/expression_editor_screen.dart';
import 'package:lamp_app/features/lamp_shell/presentation/widgets/expression_params_panel.dart';
import 'package:lamp_app/features/lamp_shell/presentation/widgets/motion_picker.dart';

import '../../_support/seed.dart';

const _devId = 'lamp-x';

/// Descriptor pulled from the sample catalog the tests seed, so the generic
/// renderer is exercised against the same JSON the firmware would emit.
ExpressionDescriptor _descriptor(String id) =>
    ExpressionCatalog.fromJson(jsonDecode(defaultExprcatJson) as Map<String, dynamic>)
        .byId(id)!;

/// Build a container with BLE seeded and inventory populated.
/// The controlNotifier is NOT pre-primed — the widget's watch primes it.
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
Future<void> _pumpToData(WidgetTester tester, String sentinel) async {
  for (var i = 0; i < 30; i++) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text(sentinel).evaluate().isNotEmpty) return;
  }
}

void main() {
  testWidgets('new-expression editor shows the catalog name + target header',
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
    expect(find.text('Breathing'), findsOneWidget);
    expect(find.text('Shade'), findsOneWidget);
    expect(find.text('Base'), findsOneWidget);
    expect(find.text('Both'), findsOneWidget);

    await tester.dragUntilVisible(
      find.text('Save'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('Save'), findsOneWidget);
  });

  testWidgets('Save adds the entry to ControlState.expressions',
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
    await tester.dragUntilVisible(
      find.text('Save'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    await tester.tap(find.text('Save'));
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
      c.read(controlNotifierProvider(_devId)).value!.expressions.expressions,
      hasLength(1),
    );
  });

  testWidgets(
      'new breathing draft seeds params from the catalog defaults',
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
    final draft = c.read(expressionDraftProvider(_devId, 'breathing', 3));
    // Literal default straight from the catalog.
    expect(draft.parameters['breathSpeed'], 10);
    // Pixel-relative default resolves to the target strip size (max shade/base
    // = 38 from the seed).
    expect(draft.parameters['size'], 38);
    // count default is a literal 1 (capped pixels only affects the max).
    expect(draft.parameters['count'], 1);
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

    c
        .read(expressionDraftProvider(_devId, 'breathing', 1).notifier)
        .update((d) => d.copyWith(intervalMin: 123));
    await tester.pump();

    await tester.tap(find.text('Base'));
    await tester.pump();

    final baseDraft = c.read(expressionDraftProvider(_devId, 'breathing', 2));
    expect(baseDraft.target, 2);
    expect(baseDraft.intervalMin, 123,
        reason: 'in-progress edits carry to the new target');

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
          targetKey: 1,
        ),
      ),
    ));
    await _pumpToData(tester, 'Breathing');

    expect(
      c.read(controlNotifierProvider(_devId)).value!.expressions.expressions,
      hasLength(1),
    );

    await tester.tap(find.text('Base'));
    await tester.pump();
    await tester.dragUntilVisible(
        find.text('Save'), find.byType(ListView), const Offset(0, -200));
    await tester.tap(find.text('Save'));
    for (var i = 0; i < 30; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    final exprs =
        c.read(controlNotifierProvider(_devId)).value!.expressions.expressions;
    expect(exprs, hasLength(1), reason: 'move must not duplicate');
    expect(exprs.single.target, 2);
  });

  testWidgets('Test previews without persisting to NVS', (tester) async {
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
            lampId: _devId, typeKey: 'glitchy', targetKey: 3),
      ),
    ));
    await _pumpToData(tester, 'Glitchy');

    await tester.tap(find.byTooltip('Test'));
    for (var i = 0; i < 10; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    expect(ble.writesTo(_devId, BleUuids.expressionOp), isEmpty,
        reason: 'Test must not upsert to NVS');
    final tests = ble.writesTo(_devId, BleUuids.expressionTest);
    final testEnvelope = utf8.decode(tests.last);
    expect(testEnvelope, contains('"a":"test_expression"'));
    expect(testEnvelope, contains('"colors"'));
    expect(testEnvelope, contains('"parameters"'));
  });

  testWidgets('Test ▶ disables for the cooldown then re-enables',
      (tester) async {
    // pulse in Trigger mode has no duration range, so the 2s fallback applies.
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
            lampId: _devId, typeKey: 'pulse', targetKey: 3),
      ),
    ));
    await _pumpToData(tester, 'Pulse');

    IconButton btn() => tester.widget<IconButton>(
        find.widgetWithIcon(IconButton, Icons.play_arrow_rounded));
    expect(btn().onPressed, isNotNull);

    await tester.tap(find.byTooltip('Test'));
    await tester.pump();
    expect(btn().onPressed, isNull);

    await tester.pump(const Duration(milliseconds: 1999));
    expect(btn().onPressed, isNull);

    await tester.pump(const Duration(milliseconds: 2));
    expect(btn().onPressed, isNotNull);
  });

  testWidgets('retargeting off then back onto the origin re-enables it',
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
            lampId: _devId, typeKey: 'breathing', targetKey: 1),
      ),
    ));
    await _pumpToData(tester, 'Breathing');

    await tester.tap(find.text('Base'));
    await tester.pump();
    await tester.tap(find.text('Shade'));
    await tester.pump();

    final draft = c.read(expressionDraftProvider(_devId, 'breathing', 1));
    expect(draft.target, 1, reason: 'origin pill stays tappable while editing');
  });

  testWidgets(
      'interval range slider renders from the catalog for trigger-based expressions',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        // glitchy declares an interval; breathing (continuous) does not.
        home: ExpressionEditorScreen(
            lampId: _devId, typeKey: 'glitchy', targetKey: 3),
      ),
    ));
    await _pumpToData(tester, 'Glitchy');
    await tester.dragUntilVisible(
      find.text('TIMING'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('TIMING'), findsOneWidget);
    // The interval range uses the catalog bounds (60..900), linear.
    final intervalSlider = find.byWidgetPredicate((w) =>
        w is RangeSlider &&
        (w.min - 60.0).abs() < 0.01 &&
        (w.max - 900.0).abs() < 0.01);
    expect(intervalSlider, findsOneWidget);
  });

  testWidgets('breathing (continuous) hides the interval control',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
            lampId: _devId, typeKey: 'breathing', targetKey: 3),
      ),
    ));
    await _pumpToData(tester, 'Breathing');
    await tester.dragUntilVisible(
      find.text('Breath cycle length'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('TRIGGER INTERVAL'), findsNothing);
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

    await tester.tap(find.text('Base'));
    await tester.pump();
    await tester.tap(find.byIcon(Icons.arrow_back));
    await tester.pump();
    await tester.pump();

    expect(find.text('Discard changes?'), findsOneWidget);
  });

  testWidgets(
      'breathing renders catalog-labelled placement + shape controls',
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
    // Optional zone defaults to whole strip; switch to Region to reveal it.
    c
        .read(expressionDraftProvider(_devId, 'breathing', 3).notifier)
        .update((d) =>
            d.copyWith(parameters: {...d.parameters, 'fullStrip': 0}));
    await tester.pump();
    await tester.dragUntilVisible(
      find.text('Spread'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('PLACEMENT'), findsOneWidget);
    // Labels come straight from the catalog descriptor.
    expect(find.text('Breath cycle length'), findsOneWidget);
    expect(find.text('Points'), findsOneWidget);
    expect(find.text('Spread'), findsOneWidget);
    expect(find.text('Together'), findsOneWidget);
    expect(find.text('Scattered'), findsOneWidget);
  });

  testWidgets('spotty renders zone, points, size, and a slow/fast speed slider',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
          lampId: _devId,
          typeKey: 'spotty',
          targetKey: 3,
        ),
      ),
    ));
    await _pumpToData(tester, 'Spotty');
    // Optional zone defaults to whole strip; switch to Region to reveal it.
    c
        .read(expressionDraftProvider(_devId, 'spotty', 3).notifier)
        .update((d) =>
            d.copyWith(parameters: {...d.parameters, 'fullStrip': 0}));
    await tester.pump();
    expect(find.text('PLACEMENT'), findsOneWidget);
    expect(find.byType(RangeSlider), findsWidgets);
    await tester.dragUntilVisible(
      find.text('Speed'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('Speed'), findsOneWidget);
    expect(find.text('slow'), findsOneWidget);
    expect(find.text('fast'), findsOneWidget);
  });

  testWidgets(
      'shifty zone is toggle-driven: whole-strip hides it, region shows it '
      'regardless of fillMode',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
          lampId: _devId,
          typeKey: 'shifty',
          targetKey: 3,
        ),
      ),
    ));
    await _pumpToData(tester, 'Shifty');
    // Whole strip is the default; the zone range is hidden even at Uniform.
    expect(find.text('Whole strip'), findsOneWidget);
    expect(find.text('Zone'), findsNothing);
    // Fill mode lives in Behaviour, below Colors.
    await tester.dragUntilVisible(
        find.text('Fill'), find.byType(ListView), const Offset(0, -200));
    expect(find.text('Fill'), findsOneWidget);
    expect(find.text('Uniform'), findsOneWidget);

    // Uniform can now be zoned: the toggle, not the fillMode, drives it.
    c
        .read(expressionDraftProvider(_devId, 'shifty', 3).notifier)
        .update((d) =>
            d.copyWith(parameters: {...d.parameters, 'fullStrip': 0}));
    await tester.pump();
    await tester.dragUntilVisible(
        find.text('Zone'), find.byType(ListView), const Offset(0, 200));
    expect(find.text('Zone'), findsOneWidget);
  });

  testWidgets(
      'glitchy Mode toggle drives the zone: hidden on Whole strip, shown on Points',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
          lampId: _devId,
          typeKey: 'glitchy',
          targetKey: 3,
        ),
      ),
    ));
    await _pumpToData(tester, 'Glitchy');
    expect(find.text('Whole strip'), findsOneWidget);
    expect(find.text('Zone'), findsNothing);
    // requiresZoning params stay hidden while whole-strip.
    expect(find.text('Points'), findsNothing);

    c
        .read(expressionDraftProvider(_devId, 'glitchy', 3).notifier)
        .update((d) =>
            d.copyWith(parameters: {...d.parameters, 'fullStrip': 0}));
    await tester.pump();
    expect(find.text('Zone'), findsOneWidget);
    await tester.dragUntilVisible(
        find.text('Points'), find.byType(ListView), const Offset(0, -200));
    expect(find.text('Points'), findsOneWidget);
    expect(find.text('Size'), findsOneWidget);
  });

  testWidgets('pulse renders placement, shape, and interval from the catalog',
      (tester) async {
    final c = await _withEmptyState();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
          lampId: _devId,
          typeKey: 'pulse',
          targetKey: 3,
        ),
      ),
    ));
    await _pumpToData(tester, 'Pulse');
    // Whole strip is the default; the zone range is hidden until Region.
    expect(find.text('Whole strip'), findsOneWidget);
    expect(find.text('Zone'), findsNothing);
    c
        .read(expressionDraftProvider(_devId, 'pulse', 3).notifier)
        .update((d) =>
            d.copyWith(parameters: {...d.parameters, 'fullStrip': 0}));
    await tester.pump();
    // Zone lives in Placement near the top; check it before scrolling away.
    expect(find.text('Zone'), findsOneWidget);
    await tester.dragUntilVisible(
      find.text('TIMING'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('TIMING'), findsOneWidget);
    await tester.dragUntilVisible(
      find.text('Pulse speed'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('Pulse speed'), findsOneWidget);
  });

  testWidgets(
      'previewZoneHighlight writes test_zone_preview payload to expressionTest',
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

    await c.read(controlNotifierProvider(_devId).future);
    final sub = c.listen(controlNotifierProvider(_devId), (_, _) {});
    addTearDown(sub.close);
    final notifier = c.read(controlNotifierProvider(_devId).notifier);

    notifier.previewZoneHighlight(
        5, 15, 3, const LampColor(r: 0xFF, g: 0x00, b: 0x00, w: 0x00));
    for (var i = 0; i < 5; i++) {
      await tester.pump(const Duration(milliseconds: 20));
    }

    final writes = ble.writesTo(_devId, BleUuids.expressionTest);
    final preview = writes
        .map((b) => jsonDecode(utf8.decode(b)) as Map<String, dynamic>)
        .firstWhere((m) => m['a'] == 'test_zone_preview', orElse: () => {});

    expect(preview['a'], 'test_zone_preview');
    expect(preview['posMin'], 5);
    expect(preview['posMax'], 15);
    expect(preview['target'], 3);
    expect(preview['color'], '#FF000000');

    await notifier.completeExpressionTest();
    for (var i = 0; i < 5; i++) {
      await tester.pump(const Duration(milliseconds: 20));
    }

    final lastWrite = jsonDecode(
            utf8.decode(ble.writesTo(_devId, BleUuids.expressionTest).last))
        as Map<String, dynamic>;
    expect(lastWrite['a'], 'test_expression_complete');
  });

  testWidgets('ExpressionParamsPanel zone slider calls onZonePreview/End',
      (tester) async {
    int? prevMin, prevMax;
    bool endCalled = false;

    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SingleChildScrollView(
          child: ExpressionParamsPanel(
            descriptor: _descriptor('spotty'),
            part: ExpressionPanelPart.placement,
            parameters: const {
              'fullStrip': 0,
              'posMin': 5,
              'posMax': 25,
              'count': 3,
              'size': 4
            },
            pixelCount: 30,
            intervalMin: 60,
            intervalMax: 900,
            onIntervalChanged: (_, _) {},
            onChanged: (_) {},
            onZonePreview: (lo, hi) {
              prevMin = lo;
              prevMax = hi;
            },
            onZonePreviewEnd: () => endCalled = true,
          ),
        ),
      ),
    ));
    await tester.pump();

    // Zone range is the first RangeSlider (Placement, above the interval).
    final zone = find.byType(RangeSlider).first;
    final sliderRect = tester.getRect(zone);
    await tester.dragFrom(
      Offset(sliderRect.left + 14, sliderRect.center.dy),
      const Offset(20, 0),
    );
    await tester.pump();

    expect(prevMin, isNotNull, reason: 'onZonePreview should fire during drag');
    expect(prevMax, isNotNull, reason: 'onZonePreview hi value should update');
    expect(endCalled, isTrue, reason: 'onZonePreviewEnd should fire on release');
  });

  testWidgets(
      'completeExpressionTest cancels pending zone-preview (quick-drag race)',
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

    await c.read(controlNotifierProvider(_devId).future);
    final sub = c.listen(controlNotifierProvider(_devId), (_, _) {});
    addTearDown(sub.close);
    final notifier = c.read(controlNotifierProvider(_devId).notifier);

    notifier.previewZoneHighlight(
        5, 15, 3, const LampColor(r: 0xFF, g: 0x00, b: 0x00, w: 0x00));
    notifier.previewZoneHighlight(
        6, 16, 3, const LampColor(r: 0xFF, g: 0x00, b: 0x00, w: 0x00));
    await notifier.completeExpressionTest();
    await tester.pump(const Duration(milliseconds: 100));

    final lastWrite = jsonDecode(
            utf8.decode(ble.writesTo(_devId, BleUuids.expressionTest).last))
        as Map<String, dynamic>;
    expect(lastWrite['a'], 'test_expression_complete',
        reason: 'pending preview must not land after the clear');
  });

  testWidgets(
      'zone-preview callback with empty-colors expression does not throw and skips write',
      (tester) async {
    final ble = InMemoryBleClient();
    SharedPreferences.setMockInitialValues({});
    await seedControlBle(
      ble,
      deviceId: _devId,
      name: 'test',
      expressionsJson:
          '[{"type":"pulse","enabled":true,"colors":[],"intervalMin":60,"intervalMax":900,"target":3}]',
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
          typeKey: 'pulse',
          targetKey: 3,
        ),
      ),
    ));
    await _pumpToData(tester, 'Pulse');

    c
        .read(expressionDraftProvider(_devId, 'pulse', 3).notifier)
        .update((d) => d.copyWith(
            colors: const [],
            parameters: {...d.parameters, 'fullStrip': 0}));
    await tester.pump();

    await tester.dragUntilVisible(
      find.text('PLACEMENT'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    await tester.pump();
    final sliderRect = tester.getRect(find.byType(RangeSlider).first);
    await tester.dragFrom(
      Offset(sliderRect.left + 14, sliderRect.center.dy),
      const Offset(20, 0),
    );
    await tester.pump(const Duration(milliseconds: 100));

    final zoneWrites = ble
        .writesTo(_devId, BleUuids.expressionTest)
        .where((b) => utf8.decode(b).contains('test_zone_preview'))
        .toList();
    expect(zoneWrites, isEmpty,
        reason: 'empty-colors guard must prevent zone preview writes');
  });

  testWidgets('Motion picker opens a sheet with 5 curve options', (tester) async {
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SingleChildScrollView(
          child: ExpressionParamsPanel(
            descriptor: _descriptor('pulse'),
            part: ExpressionPanelPart.placement,
            parameters: const {'easing': 0, 'loop': 0},
            pixelCount: 30,
            intervalMin: 60,
            intervalMax: 900,
            onIntervalChanged: (_, _) {},
            onChanged: (_) {},
          ),
        ),
      ),
    ));
    await tester.pump();

    await tester.ensureVisible(find.text('Motion'));
    await tester.tap(find.text('Motion'));
    await tester.pumpAndSettle();

    // 'Swell'..'Settle' appear only in the sheet; 'Linear' also shows in the
    // picker row (current selection), so its blurb is the sheet-only signal.
    expect(find.text('Smooth'), findsOneWidget);
    expect(find.text('Float'), findsOneWidget);
    expect(find.text('Settle'), findsOneWidget);
    expect(find.text('Swell'), findsOneWidget);
    expect(find.textContaining('Steady as she goes'), findsOneWidget);
    expect(
      find.byWidgetPredicate(
          (w) => w is CustomPaint && w.painter is EasingSparkline),
      findsNWidgets(5),
    );
  });

  testWidgets('Motion picker selection writes the easing param', (tester) async {
    Map<String, int>? written;
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SingleChildScrollView(
          child: ExpressionParamsPanel(
            descriptor: _descriptor('pulse'),
            part: ExpressionPanelPart.placement,
            parameters: const {'easing': 0, 'loop': 0},
            pixelCount: 30,
            intervalMin: 60,
            intervalMax: 900,
            onIntervalChanged: (_, _) {},
            onChanged: (p) => written = p,
          ),
        ),
      ),
    ));
    await tester.pump();

    await tester.ensureVisible(find.text('Motion'));
    await tester.tap(find.text('Motion'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Float'));
    await tester.pumpAndSettle();

    expect(written, isNotNull);
    expect(written!['easing'], 2);
  });

  testWidgets(
      'color editor previews to the target surface only and restores on close',
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

    // Base target (2): preview must hit baseColors, never shadeColors.
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: ExpressionEditorScreen(
            lampId: _devId, typeKey: 'breathing', targetKey: 2),
      ),
    ));
    await _pumpToData(tester, 'Breathing');

    await tester.dragUntilVisible(
      find.byType(ColorBlocksBar),
      find.byType(ListView),
      const Offset(0, -200),
    );
    await tester.tap(find.byType(ColorBlocksBar));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Add Color'));
    await tester.pump(const Duration(milliseconds: 100));

    expect(ble.writesTo(_devId, BleUuids.baseColors), isNotEmpty,
        reason: 'base-target preview must reach the base surface');
    expect(ble.writesTo(_devId, BleUuids.shadeColors), isEmpty,
        reason: 'base-target preview must not touch the shade surface');

    // Two Save buttons exist (editor + sheet); the sheet's is topmost.
    await tester.tap(find.widgetWithText(FilledButton, 'Save').last);
    await tester.pump(const Duration(milliseconds: 100));

    // Last base write restores the surface's opening palette (#300783FF),
    // not the previewed edit: the preview was transient.
    final baseWrites = ble.writesTo(_devId, BleUuids.baseColors);
    expect(jsonDecode(utf8.decode(baseWrites.last)), ['#300783FF'],
        reason: 'closing the sheet must restore the original base colors');

    // Drain the seen-cache commit-debounce timer before teardown.
    await tester.pump(const Duration(milliseconds: 600));
  });
}
