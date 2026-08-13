import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_blocks_bar.dart';
import 'package:lamp_app/features/control/presentation/widgets/color_stops_sheet.dart';

const _red = LampColor(r: 0xFF, g: 0, b: 0, w: 0);
const _green = LampColor(r: 0, g: 0xFF, b: 0, w: 0);

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
}
