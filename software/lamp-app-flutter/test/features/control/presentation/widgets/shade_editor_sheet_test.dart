import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../../_support/in_memory_ble_client.dart';
import '../../../../_support/seed.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/shade_editor_sheet.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/control/domain/color_rotation.dart';
import 'package:lamp_app/features/control/presentation/widgets/rotate_colors_box.dart';
import 'package:lamp_app/features/control/presentation/widgets/rotating_gradient.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'rotation_sheet_fixture.dart';

const _devId = 'aa:bb:cc:dd:ee:ff';

// Two distinct colors so removing a stop is a detectable change from baseline.
const _twoStops = '["#300783FF","#FF0000FF"]';
const _baselineColor = LampColor(r: 0x30, g: 0x07, b: 0x83, w: 0xFF);

Future<({ProviderContainer container, InMemoryBleClient ble})>
    _buildContainer() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble,
      deviceId: _devId,
      shadeColorsJson: _twoStops,
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
  return (container: c, ble: ble);
}

Widget _wrap(ProviderContainer c) => UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        theme: appTheme,
        home: Builder(
          builder: (ctx) => Scaffold(
            body: TextButton(
              onPressed: () =>
                  showShadeEditorSheet(ctx, lampId: _devId, spec: shadeSegmentSpec(0, 'Shade')),
              child: const Text('open'),
            ),
          ),
        ),
      ),
    );

void main() {
  testWidgets('Cancel reverts shade colors and closes', (tester) async {
    final (:container, :ble) = await _buildContainer();
    addTearDown(container.dispose);
    // Keep provider alive so we can read state after the sheet (and its
    // watchers) are dismissed — controlNotifierProvider is auto-dispose.
    final sub = container.listen(controlNotifierProvider(_devId), (_, _) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(container));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    expect(find.byType(ShadeEditorSheet), findsOneWidget);

    // Remove the second stop — shade.colors goes from 2 → 1.
    await tester.tap(find.byIcon(Icons.close).last);
    await tester.pumpAndSettle();

    await tester.tap(find.text('Cancel'));
    await tester.pumpAndSettle();

    expect(find.byType(ShadeEditorSheet), findsNothing);
    final colors =
        container.read(controlNotifierProvider(_devId)).value!.shade.colors;
    expect(colors.length, 2);
    expect(colors.first, _baselineColor);
  });

  testWidgets('Save commits (writes to commit char) and closes',
      (tester) async {
    final (:container, :ble) = await _buildContainer();
    addTearDown(container.dispose);
    final sub = container.listen(controlNotifierProvider(_devId), (_, _) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(container));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // Remove a stop to make an edit.
    await tester.tap(find.byIcon(Icons.close).last);
    await tester.pumpAndSettle();

    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();

    expect(ble.writesTo(_devId, BleUuids.commit), isNotEmpty);
    expect(find.byType(ShadeEditorSheet), findsNothing);
    // Edited value (1 stop) is preserved, not reverted.
    final colors =
        container.read(controlNotifierProvider(_devId)).value!.shade.colors;
    expect(colors.length, 1);
  });

  testWidgets('System-back with no changes closes immediately without dialog',
      (tester) async {
    final (:container, :ble) = await _buildContainer();
    addTearDown(container.dispose);
    final sub = container.listen(controlNotifierProvider(_devId), (_, _) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(container));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // No edits — sheet is clean.
    await tester.binding.handlePopRoute();
    await tester.pumpAndSettle();

    expect(find.text('Discard changes?'), findsNothing);
    expect(find.byType(ShadeEditorSheet), findsNothing);
  });

  testWidgets(
      'System-back with unsaved changes shows discard dialog; Discard reverts+closes',
      (tester) async {
    final (:container, :ble) = await _buildContainer();
    addTearDown(container.dispose);
    final sub = container.listen(controlNotifierProvider(_devId), (_, _) {});
    addTearDown(sub.close);

    await tester.pumpWidget(_wrap(container));
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

    expect(find.byType(ShadeEditorSheet), findsNothing);
    final colors =
        container.read(controlNotifierProvider(_devId)).value!.shade.colors;
    expect(colors.length, 2);
    expect(colors.first, _baselineColor);
  });

  group('rotation', () {
    Future<void> Function(BuildContext) openShade([int seg = 0]) =>
        (ctx) => showShadeEditorSheet(ctx,
            lampId: rotDevId, spec: shadeSegmentSpec(seg, 'Shade'));

    Future<({ProviderContainer c, InMemoryBleClient ble})> pumpShade(
      WidgetTester tester, {
      String? lampType,
      String? rotate,
      String? segments,
      int stops = 2,
      int seg = 0,
    }) async {
      final built = await buildRotationLamp(
          lampType: lampType,
          shadeStops: stops,
          shadeRotateJson: rotate,
          shadeSegmentsJson: segments);
      await tester.pumpWidget(sheetHost(built.c, openShade(seg)));
      await tester.tap(find.text('open'));
      await settleSheet(tester);
      return built;
    }

    for (final (label, lampType, rotate, shows) in [
      ('a standard lamp whose section carries rotate', 'standard', shadeRotateOff, true),
      ('a standard lamp whose section has none', 'standard', null, false),
      ('older firmware (no lampType, no rotate)', null, null, false),
      ('a lioness lamp whose section carries rotate', 'lioness', shadeRotateOff, true),
      ('a lioness lamp whose section has none', 'lioness', null, false),
    ]) {
      testWidgets('$label: box ${shows ? 'shown' : 'absent'}', (tester) async {
        await pumpShade(tester, lampType: lampType, rotate: rotate);
        expect(find.byType(RotateColorsBox), shows ? findsOneWidget : findsNothing);
      });
    }

    testWidgets('a second shade segment shows no box', (tester) async {
      await pumpShade(tester,
          lampType: 'snafu',
          rotate: shadeRotateOff,
          seg: 1,
          segments: '[{"name":"Small Dots","px":12,"colors":["#FF000000"]},'
              '{"name":"Big Dots","px":12,"colors":["#00FF0000","#0000FF00"]}]');
      expect(find.byType(RotateColorsBox), findsNothing);
    });

    testWidgets('a never-set shade shows Down', (tester) async {
      await pumpShade(tester, rotate: shadeRotateOff);
      await tapInView(tester, find.byType(Switch));
      await settleSheet(tester);
      final seg = tester.widget<SegmentedButton<int>>(find.byType(SegmentedButton<int>));
      expect(seg.selected, {1});
    });

    for (final (lampType, atBottom) in [('standard', true), (null, true), ('lioness', false)]) {
      testWidgets("lampType $lampType: the box's bar holds stop 0 at the "
          "${atBottom ? 'bottom' : 'top'}", (tester) async {
        await pumpShade(tester, lampType: lampType, rotate: shadeRotateOn);
        expect(tester.widget<RotatingGradient>(find.byType(RotatingGradient)).stop0AtBottom,
            atBottom);
      });
    }

    testWidgets('standard, Rotate on: the first colour is in the top row while '
        "the bar's stop 0 is at the bottom", (tester) async {
      await pumpShade(tester, lampType: 'standard', rotate: shadeRotateOn, stops: 3);
      final row0 = find.byKey(const ValueKey('color-stop-0'));
      final row1 = find.byKey(const ValueKey('color-stop-1'));
      expect(find.descendant(of: row0, matching: find.text('#FF0000')), findsOneWidget);
      expect(find.descendant(of: row1, matching: find.text('#00FF00')), findsOneWidget);
      expect(tester.getTopLeft(row0).dy, lessThan(tester.getTopLeft(row1).dy));
      expect(tester.widget<RotatingGradient>(find.byType(RotatingGradient)).stop0AtBottom,
          isTrue);
    });

    testWidgets('Save persists the rotation through CHAR_COMMIT', (tester) async {
      final (:c, :ble) = await pumpShade(tester, rotate: shadeRotateOff);
      await tapInView(tester, find.byType(Switch));
      await settleSheet(tester);
      await tapInView(tester, find.widgetWithText(FilledButton, 'Save'));
      await settleSheet(tester);
      expect(find.byType(ShadeEditorSheet), findsNothing);
      expect(ble.writesTo(rotDevId, BleUuids.commit), isNotEmpty);
      final last = String.fromCharCodes(ble.writesTo(rotDevId, BleUuids.shadeColors).last);
      expect(last, contains('"rotate":{"on":true,"periodS":40,"dir":"down"}'));
    });

    testWidgets('Cancel restores the opened rotation on the lamp and in the app',
        (tester) async {
      final (:c, :ble) = await pumpShade(tester, rotate: shadeRotateOn);
      final opened = c.read(controlNotifierProvider(rotDevId)).value!.shade.rotation;
      await tapInView(tester, find.text('Up'));
      await settleSheet(tester);
      expect(c.read(controlNotifierProvider(rotDevId)).value!.shade.rotation!.direction,
          RotationDirection.up);
      await tapInView(tester, find.text('Cancel'));
      await settleSheet(tester);
      expect(c.read(controlNotifierProvider(rotDevId)).value!.shade.rotation, opened);
      final last = String.fromCharCodes(ble.writesTo(rotDevId, BleUuids.shadeColors).last);
      expect(last, contains('"rotate":{"on":true,"periodS":40,"dir":"down"}'));
    });

    group('sheet height', () {
      sheetSizingCases(
        build: (n, rotate) async =>
            (await buildRotationLamp(shadeStops: n, shadeRotateJson: rotate)).c,
        open: openShade(),
        rotateOn: shadeRotateOn,
        rotateOff: shadeRotateOff,
      );
    });
  });
}
