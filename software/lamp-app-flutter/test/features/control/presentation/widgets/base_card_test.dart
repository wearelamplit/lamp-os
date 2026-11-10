import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/presentation/widgets/base_card.dart';
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
}
