import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/domain/color_rotation.dart';
import 'package:lamp_app/features/control/presentation/widgets/base_card.dart';
import 'package:lamp_app/features/control/presentation/widgets/rotating_gradient.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../_support/seed.dart';

const _devId = 'aa:bb:cc:dd:ee:ff';

Future<ProviderContainer> _buildContainer({
  String baseColorsJson = '["#300783FF","#FF0000FF"]',
  String? lampType,
  String? baseSegmentsJson,
  String? baseRotateJson,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble,
      deviceId: _devId,
      baseColorsJson: baseColorsJson,
      lampType: lampType,
      baseSegmentsJson: baseSegmentsJson,
      baseRotateJson: baseRotateJson);
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
  testWidgets('renders the "Base" label and no "stops" subtitle',
      (tester) async {
    final c = await _buildContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: BaseCard(lampId: _devId, onTap: () {}),
        ),
      ),
    ));
    await tester.pumpAndSettle();
    expect(find.text('Base'), findsOneWidget);
    expect(find.textContaining('stops'), findsNothing);
  });

  testWidgets('does not render per-color circle chips', (tester) async {
    final c = await _buildContainer(
        baseColorsJson: '["#300783FF","#FF0000FF","#00FF00FF"]');
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: BaseCard(lampId: _devId, onTap: () {}),
        ),
      ),
    ));
    await tester.pumpAndSettle();
    final circles = tester
        .widgetList<Container>(find.descendant(
            of: find.byType(BaseCard), matching: find.byType(Container)))
        .where((c) {
      final d = c.decoration;
      return d is BoxDecoration && d.shape == BoxShape.circle;
    }).toList();
    expect(circles, isEmpty);
  });

  testWidgets('still renders the gradient ribbon container', (tester) async {
    final c = await _buildContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: BaseCard(lampId: _devId, onTap: () {}),
        ),
      ),
    ));
    await tester.pumpAndSettle();
    final hasGradient = tester
        .widgetList<Container>(find.descendant(
            of: find.byType(BaseCard), matching: find.byType(Container)))
        .where((c) {
      final d = c.decoration;
      return d is BoxDecoration && d.gradient is LinearGradient;
    }).toList();
    expect(hasGradient, isNotEmpty);
  });

  group('still swatch stop order', () {
    const rgb = '["#FF000000","#00FF0000","#0000FF00"]';
    for (final (lampType, segs) in [
      ('standard', null),
      ('snafu', '[{"name":"Stem","px":35,"colors":$rgb}]'),
    ]) {
      testWidgets('$lampType: top to bottom in list order', (tester) async {
        final c = await _buildContainer(
            lampType: lampType, baseColorsJson: rgb, baseSegmentsJson: segs);
        addTearDown(c.dispose);
        await tester.pumpWidget(UncontrolledProviderScope(
          container: c,
          child: MaterialApp(
            home: Scaffold(body: BaseCard(lampId: _devId, onTap: () {})),
          ),
        ));
        await tester.pumpAndSettle();
        final g = tester
            .widgetList<Container>(find.byType(Container))
            .map((b) => b.decoration)
            .whereType<BoxDecoration>()
            .map((d) => d.gradient)
            .whereType<LinearGradient>()
            .single;
        expect(g.begin, Alignment.topCenter);
        expect(g.end, Alignment.bottomCenter);
        expect(g.colors, const [
          Color(0xFFFF0000),
          Color(0xFF00FF00),
          Color(0xFF0000FF),
        ]);
      });
    }
  });

  group('rotating swatch', () {
    const rgb = '["#FF000000","#00FF0000","#0000FF00"]';
    const on40 = '{"on":true,"periodS":40,"dir":"up"}';
    const stem = '[{"name":"Stem","px":35,"colors":$rgb}]';

    Future<void> pumpCard(WidgetTester tester, ProviderContainer c,
        {bool reduceMotion = false}) async {
      await tester.pumpWidget(UncontrolledProviderScope(
        container: c,
        child: MediaQuery(
          data: MediaQueryData(disableAnimations: reduceMotion),
          child: MaterialApp(
            home: Scaffold(body: BaseCard(lampId: _devId, onTap: () {})),
          ),
        ),
      ));
      // Elapses the provider container's zero-length dispose timer.
      await tester.pump(const Duration(milliseconds: 16));
    }

    for (final lampType in ['standard', 'snafu']) {
      testWidgets('$lampType, on with 2+ colours: animates, stop 0 at the top, '
          'reads "Rotating · 40s loop"', (tester) async {
        final c = await _buildContainer(
            lampType: lampType,
            baseColorsJson: rgb,
            baseSegmentsJson: lampType == 'snafu' ? stem : null,
            baseRotateJson: on40);
        addTearDown(c.dispose);
        await pumpCard(tester, c);
        final g = tester.widget<RotatingGradient>(find.byType(RotatingGradient));
        expect(g.stop0AtBottom, isFalse);
        expect(tester.binding.transientCallbackCount, greaterThan(0));
        expect(find.text('Rotating · 40s loop'), findsOneWidget);
      });
    }

    testWidgets('a 65 s period reads "Rotating · 1m 5s loop"', (tester) async {
      final c = await _buildContainer(
          baseColorsJson: rgb,
          baseRotateJson: '{"on":true,"periodS":65,"dir":"up"}');
      addTearDown(c.dispose);
      await pumpCard(tester, c);
      expect(find.text('Rotating · 1m 5s loop'), findsOneWidget);
    });

    for (final (label, lampType, colors, rotate) in [
      ('rotate off', 'standard', rgb, '{"on":false,"periodS":40,"dir":"up"}'),
      ('one colour', 'standard', '["#FF000000"]', on40),
      ('no rotate in the section (standard)', 'standard', rgb, null),
      ('no rotate in the section (snafu)', 'snafu', rgb, null),
    ]) {
      testWidgets('$label: still swatch, no subtitle', (tester) async {
        final c = await _buildContainer(
            lampType: lampType, baseColorsJson: colors, baseRotateJson: rotate);
        addTearDown(c.dispose);
        await pumpCard(tester, c);
        expect(find.byType(RotatingGradient), findsNothing);
        expect(find.textContaining('Rotating'), findsNothing);
        expect(tester.binding.transientCallbackCount, 0);
      });
    }

    testWidgets('reduce-motion: holds still, subtitle stays', (tester) async {
      final c = await _buildContainer(baseColorsJson: rgb, baseRotateJson: on40);
      addTearDown(c.dispose);
      await pumpCard(tester, c, reduceMotion: true);
      expect(find.byType(RotatingGradient), findsOneWidget);
      expect(tester.binding.transientCallbackCount, 0);
      expect(find.text('Rotating · 40s loop'), findsOneWidget);
    });

    testWidgets('a rotation-only change reaches the card live', (tester) async {
      final c = await _buildContainer(
          baseColorsJson: rgb,
          baseRotateJson: '{"on":false,"periodS":40,"dir":"up"}');
      addTearDown(c.dispose);
      await pumpCard(tester, c);
      expect(find.byType(RotatingGradient), findsNothing);
      await c.read(controlNotifierProvider(_devId).notifier).setBaseRotation(
          const ColorRotation(
              on: true, periodS: 65, direction: RotationDirection.up));
      // Past the notifier's 60 ms write window, so its timer is spent.
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.byType(RotatingGradient), findsOneWidget);
      expect(find.text('Rotating · 1m 5s loop'), findsOneWidget);
    });
  });
}
