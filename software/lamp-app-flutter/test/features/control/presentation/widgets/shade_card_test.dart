import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/presentation/widgets/shade_card.dart';
import 'package:lamp_app/features/control/presentation/widgets/shade_editor_sheet.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../_support/seed.dart';

const _devId = 'aa:bb:cc:dd:ee:ff';

const _rgb = '["#FF000000","#00FF0000","#0000FF00"]';
const _red = Color(0xFFFF0000);
const _green = Color(0xFF00FF00);
const _blue = Color(0xFF0000FF);

Future<ProviderContainer> _buildContainer({
  String? lampType,
  String shadeColorsJson = '["#300783FF"]',
  String? shadeSegmentsJson,
  String? shadeRotateJson,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble,
      deviceId: _devId,
      lampType: lampType,
      shadeColorsJson: shadeColorsJson,
      shadeSegmentsJson: shadeSegmentsJson,
      shadeRotateJson: shadeRotateJson,
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

Widget _cards(ProviderContainer c, List<int> segs) => UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: Column(children: [
            for (final i in segs)
              ShadeCard(lampId: _devId, spec: shadeSegmentSpec(i, 'Shade $i')),
          ]),
        ),
      ),
    );

/// The still swatch gradient of each ShadeCard, in card order.
List<LinearGradient> _swatches(WidgetTester tester) => [
      for (final card in find.byType(ShadeCard).evaluate())
        for (final box in tester.widgetList<Container>(find.descendant(
            of: find.byWidget(card.widget), matching: find.byType(Container))))
          if (box.decoration case BoxDecoration(gradient: final LinearGradient g))
            g,
    ];

void main() {
  testWidgets('renders the "Shade" label and no hex string', (tester) async {
    final c = await _buildContainer();
    addTearDown(c.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: ShadeCard(lampId: _devId, spec: shadeSegmentSpec(0,'Shade')),
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
      child: MaterialApp(
        home: Scaffold(
          body: ShadeCard(lampId: _devId, spec: shadeSegmentSpec(0,'Shade')),
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

  group('still swatch stop order', () {
    for (final lampType in ['standard', null]) {
      testWidgets('lampType $lampType: stop 0 at the bottom, list order',
          (tester) async {
        final c = await _buildContainer(lampType: lampType, shadeColorsJson: _rgb);
        addTearDown(c.dispose);
        await tester.pumpWidget(_cards(c, [0]));
        await tester.pumpAndSettle();
        final g = _swatches(tester).single;
        expect(g.begin, Alignment.bottomCenter);
        expect(g.end, Alignment.topCenter);
        expect(g.colors, [_red, _green, _blue]);
      });
    }

    testWidgets('snafu with two shade segments: both cards top to bottom',
        (tester) async {
      final c = await _buildContainer(
        lampType: 'snafu',
        shadeColorsJson: _rgb,
        shadeSegmentsJson: '[{"name":"Small Dots","px":12,"colors":$_rgb},'
            '{"name":"Big Dots","px":12,"colors":$_rgb}]',
      );
      addTearDown(c.dispose);
      await tester.pumpWidget(_cards(c, [0, 1]));
      await tester.pumpAndSettle();
      final gs = _swatches(tester);
      expect(gs, hasLength(2));
      for (final g in gs) {
        expect(g.begin, Alignment.topCenter);
        expect(g.end, Alignment.bottomCenter);
        expect(g.colors, [_red, _green, _blue]);
      }
    });

    testWidgets('lioness (the standard fixture but its lampType): top to bottom',
        (tester) async {
      final c = await _buildContainer(lampType: 'lioness', shadeColorsJson: _rgb);
      addTearDown(c.dispose);
      await tester.pumpWidget(_cards(c, [0]));
      await tester.pumpAndSettle();
      final g = _swatches(tester).single;
      expect(g.begin, Alignment.topCenter);
      expect(g.end, Alignment.bottomCenter);
      expect(g.colors, [_red, _green, _blue]);
    });
  });
}
