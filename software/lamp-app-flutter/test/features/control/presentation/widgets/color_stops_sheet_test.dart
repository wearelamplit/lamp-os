import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/app_sheet.dart';
import 'package:lamp_app/features/control/domain/color_rotation.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_blocks_bar.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_stops_sheet.dart';
import 'package:lamp_app/features/control/presentation/widgets/rotate_colors_box.dart';

import 'rotation_sheet_fixture.dart';

const _red = LampColor(r: 0xFF, g: 0, b: 0, w: 0);
const _green = LampColor(r: 0, g: 0xFF, b: 0, w: 0);
const _opened = ColorRotation(
    on: true, periodS: 40, direction: RotationDirection.down);

/// Opens a content-sized sheet carrying [rotation], recording every live
/// rotation edit into [edits].
Future<void> _openRotating(
  WidgetTester tester, {
  required List<ColorRotation> edits,
  ColorRotation? rotation = _opened,
  List<LampColor> initial = const [_red, _green],
}) async {
  await tester.pumpWidget(MaterialApp(
    theme: appTheme,
    home: Builder(
      builder: (ctx) => Scaffold(
        body: TextButton(
          onPressed: () => showAppSheet<void>(
            ctx,
            builder: (_) => ColorSheetHeight(
              child: ColorStopsSheet(
                initial: initial,
                title: 'Base colors',
                max: 6,
                contentSized: true,
                rotation: rotation,
                onRotationChanged: edits.add,
                onChanged: (_) {},
                onSave: (_) async {},
              ),
            ),
          ),
          child: const Text('open'),
        ),
      ),
    ),
  ));
  await tester.tap(find.text('open'));
  await settleSheet(tester);
}

void main() {
  testWidgets('ColorBlocksBar renders one block per color and fires onTap',
      (tester) async {
    var taps = 0;
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: ColorBlocksBar(
          colors: const [_red, _green],
          onTap: () => taps++,
        ),
      ),
    ));
    expect(
      find.descendant(
        of: find.byType(ColorBlocksBar),
        matching: find.byType(ColoredBox),
      ),
      findsNWidgets(2),
    );
    await tester.tap(find.byType(ColorBlocksBar));
    expect(taps, 1);
  });

  testWidgets('ColorBlocksBar shows the empty state with no colors',
      (tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: Scaffold(body: ColorBlocksBar(colors: [])),
    ));
    expect(
      find.descendant(
        of: find.byType(ColorBlocksBar),
        matching: find.byType(ColoredBox),
      ),
      findsNothing,
    );
    expect(find.text('Tap to set colors'), findsOneWidget);
  });

  testWidgets(
      'ColorStopsSheet non-reorderable: no drag handles, add streams onChanged, '
      'Save commits and closes', (tester) async {
    final changes = <List<LampColor>>[];
    List<LampColor>? saved;

    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Builder(
        builder: (ctx) => Scaffold(
          body: TextButton(
            onPressed: () => showColorStopsSheet(
              ctx,
              initial: const [_red, _green],
              title: 'Palette',
              max: 4,
              reorderable: false,
              onChanged: (c) => changes.add(c),
              onSave: (c) async => saved = c,
            ),
            child: const Text('open'),
          ),
        ),
      ),
    ));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    expect(find.text('Palette'), findsOneWidget);
    expect(find.byIcon(Icons.drag_indicator), findsNothing);

    await tester.tap(find.text('Add Color'));
    await tester.pumpAndSettle();
    expect(changes.last.length, 3);

    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();
    expect(saved, isNotNull);
    expect(saved!.length, 3);
    expect(find.text('Palette'), findsNothing);
  });

  testWidgets('ColorStopsSheet Cancel reverts the live preview to initial',
      (tester) async {
    List<LampColor>? lastPreview;

    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Builder(
        builder: (ctx) => Scaffold(
          body: TextButton(
            onPressed: () => showColorStopsSheet(
              ctx,
              initial: const [_red, _green],
              title: 'Palette',
              max: 4,
              reorderable: false,
              onChanged: (c) => lastPreview = c,
              onSave: (_) async {},
            ),
            child: const Text('open'),
          ),
        ),
      ),
    ));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    // Remove a stop, then cancel — the last preview restores the two-stop start.
    await tester.tap(find.byIcon(Icons.close).last);
    await tester.pumpAndSettle();
    expect(lastPreview!.length, 1);

    await tester.tap(find.text('Cancel'));
    await tester.pumpAndSettle();
    expect(lastPreview!.length, 2);
    expect(find.text('Palette'), findsNothing);
  });

  group('rotation', () {
    testWidgets('no box when rotation is null', (tester) async {
      await _openRotating(tester, edits: [], rotation: null);
      expect(find.byType(RotateColorsBox), findsNothing);
      expect(find.text('Rotate colors'), findsNothing);
    });

    testWidgets('under 2 colours the box greys and keeps the setting; '
        'adding one re-enables it as it was', (tester) async {
      final edits = <ColorRotation>[];
      await _openRotating(tester, edits: edits,
          rotation: _opened.copyWith(periodS: 65));
      expect(find.byType(Slider), findsOneWidget);

      await tapInView(tester, find.byIcon(Icons.close).last);
      await settleSheet(tester);
      expect(find.text('Add a second color to rotate.'), findsOneWidget);
      final sw = tester.widget<Switch>(find.byType(Switch));
      expect(sw.value, isTrue);
      expect(sw.onChanged, isNull);
      expect(find.byType(Slider), findsNothing);

      await tapInView(tester, find.text('Add Color'));
      await settleSheet(tester);
      expect(find.text('Add a second color to rotate.'), findsNothing);
      expect(find.byType(Slider), findsOneWidget);
      expect(find.text('1m 5s'), findsOneWidget);
      expect(edits, isEmpty);
    });

    testWidgets('a rotation-only change is dirty: back asks to discard, '
        'Discard restores the opened rotation', (tester) async {
      final edits = <ColorRotation>[];
      await _openRotating(tester, edits: edits);
      await tapInView(tester, find.byType(Switch));
      await settleSheet(tester);
      expect(edits.last.on, isFalse);

      await tester.binding.handlePopRoute();
      await settleSheet(tester);
      expect(find.text('Discard changes?'), findsOneWidget);
      await tester.tap(find.text('Discard'));
      await settleSheet(tester);
      expect(edits.last, _opened);
      expect(find.byType(ColorStopsSheet), findsNothing);
    });

    testWidgets('Cancel after a direction change restores the opened direction',
        (tester) async {
      final edits = <ColorRotation>[];
      await _openRotating(tester, edits: edits);
      await tapInView(tester, find.text('Up'));
      await settleSheet(tester);
      expect(edits.last.direction, RotationDirection.up);
      await tapInView(tester, find.text('Cancel'));
      await settleSheet(tester);
      expect(edits.last, _opened);
    });

    testWidgets('Cancel with no rotation change sends no rotation',
        (tester) async {
      final edits = <ColorRotation>[];
      await _openRotating(tester, edits: edits);
      await tapInView(tester, find.byIcon(Icons.close).last);
      await settleSheet(tester);
      await tapInView(tester, find.text('Cancel'));
      await settleSheet(tester);
      expect(edits, isEmpty);
    });

    testWidgets('the Direction control shows only while Rotate is on, and a '
        'tap reaches onRotationChanged', (tester) async {
      final edits = <ColorRotation>[];
      await _openRotating(tester, edits: edits,
          rotation: _opened.copyWith(on: false));
      expect(find.text('Direction'), findsNothing);
      await tapInView(tester, find.byType(Switch));
      await settleSheet(tester);
      expect(find.text('Direction'), findsOneWidget);
      await tapInView(tester, find.text('Up'));
      await settleSheet(tester);
      expect(edits.last,
          const ColorRotation(on: true, periodS: 40, direction: RotationDirection.up));
    });
  });

  group('showColorStopsSheet is a fixed 0.6 high', () {
    for (final n in [2, 6]) {
      testWidgets('$n colours', (tester) async {
        useSmallPhone(tester);
        await tester.pumpWidget(MaterialApp(
          theme: appTheme,
          home: Builder(
            builder: (ctx) => Scaffold(
              body: TextButton(
                onPressed: () => showColorStopsSheet(
                  ctx,
                  initial: List.filled(n, _red),
                  title: 'Palette',
                  max: 8,
                  onSave: (_) async {},
                ),
                child: const Text('open'),
              ),
            ),
          ),
        ));
        await tester.tap(find.text('open'));
        await tester.pumpAndSettle();
        expect(sheetHeight(tester), closeTo(0.6 * sheetRoom(tester), 1));
        final list = tester.state<ScrollableState>(find.descendant(
            of: find.byType(ReorderableListView),
            matching: find.byType(Scrollable)).first);
        if (n == 6) expect(list.position.maxScrollExtent, greaterThan(0));
      });
    }
  });
}
