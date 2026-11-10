import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_stops_sheet.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../_support/in_memory_ble_client.dart';
import '../../../../_support/seed.dart';

const rotDevId = 'aa:bb:cc:dd:ee:ff';

/// A never-set shade and base, as the firmware sends them.
const shadeRotateOff = '{"on":false,"periodS":40,"dir":"down"}';
const baseRotateOff = '{"on":false,"periodS":40,"dir":"up"}';
const shadeRotateOn = '{"on":true,"periodS":40,"dir":"down"}';
const baseRotateOn = '{"on":true,"periodS":40,"dir":"up"}';

/// `n` distinct colours as a section JSON array, first red then green, blue.
String colorsJson(int n) {
  const hexes = [
    '#FF000000', '#00FF0000', '#0000FF00',
    '#FFFF0000', '#00FFFF00', '#FF00FF00',
  ];
  return '[${hexes.take(n).map((h) => '"$h"').join(',')}]';
}

/// Seeds a lamp, keeps its control notifier alive for the test and returns
/// the container and the fake BLE client.
Future<({ProviderContainer c, InMemoryBleClient ble})> buildRotationLamp({
  String? lampType,
  int shadeStops = 2,
  int baseStops = 2,
  String? shadeRotateJson,
  String? baseRotateJson,
  String? shadeSegmentsJson,
  String? baseSegmentsJson,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble,
      deviceId: rotDevId,
      lampType: lampType,
      shadeColorsJson: colorsJson(shadeStops),
      baseColorsJson: colorsJson(baseStops),
      shadeRotateJson: shadeRotateJson,
      baseRotateJson: baseRotateJson,
      shadeSegmentsJson: shadeSegmentsJson,
      baseSegmentsJson: baseSegmentsJson);
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: rotDevId,
        name: 'jacko',
        controlPassword: '',
      ));
  final sub = c.listen(controlNotifierProvider(rotDevId), (_, _) {});
  addTearDown(c.dispose);
  addTearDown(sub.close);
  await c.read(controlNotifierProvider(rotDevId).future);
  return (c: c, ble: ble);
}

/// A 375 × 667 pt phone, or another size.
void useSmallPhone(WidgetTester tester, [Size size = const Size(375, 667)]) {
  tester.view.physicalSize = size;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
}

Widget sheetHost(
  ProviderContainer c,
  Future<void> Function(BuildContext) open,
) =>
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        theme: appTheme,
        home: Builder(
          builder: (ctx) => Scaffold(
            body: TextButton(
              onPressed: () => open(ctx),
              child: const Text('open'),
            ),
          ),
        ),
      ),
    );

/// Runs a sheet's open or close animation. A rotating preview keeps a ticker
/// going, so pumpAndSettle would never return.
Future<void> settleSheet(WidgetTester tester) async {
  for (var i = 0; i < 10; i++) {
    await tester.pump(const Duration(milliseconds: 100));
  }
}

/// The colour sheet's own height, under the theme's drag handle.
double sheetHeight(WidgetTester tester) =>
    tester.getSize(find.byType(ColorStopsSheet)).height;

/// The height the modal offers its content, which the 0.6 / 0.9 factors
/// scale: the constraint on the sheet's height box.
double sheetRoom(WidgetTester tester) {
  final box = find.ancestor(
    of: find.byType(ColorStopsSheet),
    matching: find.byWidgetPredicate(
        (w) => w is FractionallySizedBox || w is ColorSheetHeight),
  );
  return (tester.renderObject(box.first) as RenderBox).constraints.maxHeight;
}

/// Scrolls [finder] into view, then taps it.
Future<void> tapInView(WidgetTester tester, Finder finder) async {
  await tester.ensureVisible(finder);
  await tester.pump();
  await tester.tap(finder);
}

/// No layout overflow was reported.
void expectNoOverflow(WidgetTester tester) =>
    expect(tester.takeException(), isNull);

/// Every row from `color-stop-0` to `color-stop-<n-1>` lies inside the sheet.
void expectRowsInsideSheet(WidgetTester tester, int n) {
  final sheet = tester.getRect(find.byType(ColorStopsSheet));
  for (var i = 0; i < n; i++) {
    final row = tester.getRect(find.byKey(ValueKey('color-stop-$i')));
    expect(row.top, greaterThanOrEqualTo(sheet.top - 0.5), reason: 'row $i');
    expect(row.bottom, lessThanOrEqualTo(sheet.bottom + 0.5), reason: 'row $i');
  }
}

/// The sizing cases shared by the Base and Shade sheets. [build] seeds a lamp with `n` colours on the role under test and its
/// `rotate` JSON (null for a role that cannot rotate); [open] opens its sheet.
void sheetSizingCases({
  required Future<ProviderContainer> Function(int n, String? rotate) build,
  required Future<void> Function(BuildContext) open,
  required String rotateOn,
  required String rotateOff,
}) {
  Future<void> pumpSheet(WidgetTester tester, int n, String? rotate) async {
    useSmallPhone(tester);
    final c = await build(n, rotate);
    await tester.pumpWidget(sheetHost(c, open));
    await tester.tap(find.text('open'));
    await settleSheet(tester);
  }

  testWidgets('6 colours, Rotate on: no overflow, at most 0.9, scrolls to the '
      'last row and Save', (tester) async {
    await pumpSheet(tester, 6, rotateOn);
    expectNoOverflow(tester);
    final room = sheetRoom(tester);
    expect(sheetHeight(tester), lessThanOrEqualTo(0.9 * room + 0.5));
    final sheet = tester.getRect(find.byType(ColorStopsSheet));
    for (final target in [
      find.byKey(const ValueKey('color-stop-5')),
      find.widgetWithText(FilledButton, 'Save'),
    ]) {
      await tester.ensureVisible(target);
      await tester.pump();
      final r = tester.getRect(target);
      expect(r.bottom, lessThanOrEqualTo(sheet.bottom + 0.5));
      expect(r.top, greaterThanOrEqualTo(sheet.top - 0.5));
    }
    expectNoOverflow(tester);
  });

  for (final (label, rotate) in [
    ('Rotate off', rotateOff),
    ('no rotate in the section', null),
  ]) {
    testWidgets('5 colours, $label: taller than 0.6, at most 0.9, every row '
        'shows', (tester) async {
      await pumpSheet(tester, 5, rotate);
      expectNoOverflow(tester);
      final room = sheetRoom(tester);
      expect(sheetHeight(tester), greaterThan(0.6 * room + 1));
      expect(sheetHeight(tester), lessThanOrEqualTo(0.9 * room + 0.5));
      expectRowsInsideSheet(tester, 5);
    });
  }

  // Two colours fit in 0.6 on the 375 × 667 phone without the Rotate box;
  // with it they fit on the mockup's 390 × 844 phone.
  for (final (label, rotate, size) in [
    ('no rotate in the section, 375 × 667', null, const Size(375, 667)),
    ('Rotate off, 390 × 844', rotateOff, const Size(390, 844)),
  ]) {
    testWidgets('2 colours, $label: 0.6, Save at the bottom',
        (tester) async {
      useSmallPhone(tester, size);
      final c = await build(2, rotate);
      await tester.pumpWidget(sheetHost(c, open));
      await tester.tap(find.text('open'));
      await settleSheet(tester);
      expectNoOverflow(tester);
      expect(sheetHeight(tester), closeTo(0.6 * sheetRoom(tester), 1));
      expectSaveAtBottom(tester);
    });
  }

  testWidgets('2 colours, Rotate off, 375 × 667: the box pushes the content '
      'past 0.6 and the sheet hugs it', (tester) async {
    await pumpSheet(tester, 2, rotateOff);
    expectNoOverflow(tester);
    final room = sheetRoom(tester);
    expect(sheetHeight(tester), greaterThan(0.6 * room + 1));
    expect(sheetHeight(tester), lessThan(0.9 * room - 1));
    expectRowsInsideSheet(tester, 2);
    expectSaveAtBottom(tester);
  });
}

/// Save sits at the foot of the sheet, under only its padding.
void expectSaveAtBottom(WidgetTester tester) {
  final sheet = tester.getRect(find.byType(ColorStopsSheet));
  final save = tester.getRect(find.widgetWithText(FilledButton, 'Save'));
  expect(sheet.bottom - save.bottom, lessThan(48));
}
