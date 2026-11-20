import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_blocks_bar.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_stops_sheet.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/presentation/widgets/wisp_manual_palette.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/wisp_palette_ble.dart';

const _lampId = 'palette-sheet-lamp';
const _p = [
  LampColor(r: 200, g: 0, b: 30, w: 0),
  LampColor(r: 0, g: 60, b: 90, w: 15),
];
const _other = [LampColor(r: 90, g: 90, b: 0, w: 40)];

/// The Manual palette sheet's Save while the editor behind it waits on the
/// wisp's palette.
void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  late PaletteBle ble;
  late WispNotifier n;

  // The notifier runs under runAsync, so its BLE callbacks need the real
  // event loop drained before frames rebuild the widgets. The loading line's
  // spinner never settles, so frames are pumped by time.
  Future<void> settle(WidgetTester tester) async {
    for (var i = 0; i < 5; i++) {
      await tester.runAsync(pumpEventQueue);
      await tester.pump(const Duration(milliseconds: 16));
    }
    await tester.pump(const Duration(milliseconds: 300));
  }

  Future<void> openSheet(WidgetTester tester) async {
    ble = PaletteBle(_lampId)..seedPalette(_p);
    late ProviderContainer c;
    await tester.runAsync(() async {
      (c, n) = await openWisp(ble, statingStatus(wispA, _p));
    });
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          theme: appTheme,
          home: const Scaffold(body: ManualPaletteEditor(lampId: _lampId)),
        ),
      ),
    );
    await settle(tester);
    await tester.tap(find.byType(ColorBlocksBar));
    await tester.pumpAndSettle();
  }

  Finder inSheet(Finder f) =>
      find.descendant(of: find.byType(ColorStopsSheet), matching: f);

  bool saveEnabled(WidgetTester tester) => tester
      .widget<FilledButton>(inSheet(find.widgetWithText(FilledButton, 'Save')))
      .enabled;

  testWidgets('an open sheet greys Save with a note while the palette is '
      'unread, and gives it back with the edit kept when the read lands',
      (tester) async {
    await openSheet(tester);
    expect(saveEnabled(tester), isTrue);
    expect(inSheet(find.text('Reading palette from wisp…')), findsNothing);
    await tester.tap(inSheet(find.text('Add Color')));
    await tester.pumpAndSettle();

    await tester.runAsync(() async {
      await ble.disconnect(_lampId);
      await ble.connect(_lampId);
    });
    await settle(tester);

    expect(n.paletteLoading, isTrue);
    expect(saveEnabled(tester), isFalse);
    expect(inSheet(find.text('Reading palette from wisp…')), findsOneWidget);
    await tester.tap(inSheet(find.widgetWithText(FilledButton, 'Save')));
    await settle(tester);
    expect(ble.paletteWrites, isEmpty);
    expect(n.manualPaletteWriteFailed, isFalse);

    await tester.runAsync(() async {
      ble
        ..seedPalette(_other)
        ..notifyStatus(statingStatus(wispA, _other));
    });
    await settle(tester);

    expect(n.paletteLoading, isFalse);
    expect(find.byType(ColorStopsSheet), findsOneWidget);
    expect(saveEnabled(tester), isTrue);
    expect(inSheet(find.text('Reading palette from wisp…')), findsNothing);

    await tester.tap(inSheet(find.widgetWithText(FilledButton, 'Save')));
    await settle(tester);
    final op = jsonDecode(utf8.decode(ble.paletteWrites.single))
        as Map<String, dynamic>;
    expect(op['colors'], hasLength(_p.length + 1));

    await tester.runAsync(() async {
      ble.notifyStatus(echoOfLastPaletteSave(ble, _lampId));
    });
    await settle(tester);
    expect(n.manualPaletteDirty, isFalse);
  });
}
