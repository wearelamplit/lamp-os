import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/lamp_shell/domain/easing_curves.dart';
import 'package:lamp_app/features/lamp_shell/domain/expression_catalog.dart';
import 'package:lamp_app/features/lamp_shell/presentation/widgets/motion_picker.dart';

const _options = [
  EnumOption(value: 0, label: 'Linear', group: 'Linear'),
  EnumOption(value: 4, label: 'In', group: 'Smooth'),
  EnumOption(value: 3, label: 'Out', group: 'Smooth'),
  EnumOption(value: 1, label: 'In-out', group: 'Smooth'),
  EnumOption(value: 2, label: 'Float', group: 'Float'),
  EnumOption(value: 17, label: 'Random', group: 'Random'),
];

// Mirrors the production `kEasingOptions` catalog order: calm-first family
// sequence Linear, Smooth, Snap, Float, Overshoot, Spring, Bounce, Random.
const _fullOptions = [
  EnumOption(value: 0, label: 'Linear', group: 'Linear'),
  EnumOption(value: 4, label: 'In', group: 'Smooth'),
  EnumOption(value: 3, label: 'Out', group: 'Smooth'),
  EnumOption(value: 1, label: 'In-out', group: 'Smooth'),
  EnumOption(value: 5, label: 'In', group: 'Snap'),
  EnumOption(value: 6, label: 'Out', group: 'Snap'),
  EnumOption(value: 7, label: 'In-out', group: 'Snap'),
  EnumOption(value: 2, label: 'Float', group: 'Float'),
  EnumOption(value: 8, label: 'In', group: 'Overshoot'),
  EnumOption(value: 9, label: 'Out', group: 'Overshoot'),
  EnumOption(value: 10, label: 'In-out', group: 'Overshoot'),
  EnumOption(value: 11, label: 'In', group: 'Spring'),
  EnumOption(value: 12, label: 'Out', group: 'Spring'),
  EnumOption(value: 13, label: 'In-out', group: 'Spring'),
  EnumOption(value: 14, label: 'In', group: 'Bounce'),
  EnumOption(value: 15, label: 'Out', group: 'Bounce'),
  EnumOption(value: 16, label: 'In-out', group: 'Bounce'),
  EnumOption(value: 17, label: 'Random', group: 'Random'),
];

Widget _pump(int value, ValueChanged<int> onChanged) => MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: MotionPicker(
          label: 'Motion',
          options: _options,
          value: value,
          onChanged: onChanged,
        ),
      ),
    );

void main() {
  testWidgets(
      'sheet shows a Direction row and one tile per family; '
      'tapping Direction Out then Motion Smooth calls onChanged with the SmoothOut value',
      (tester) async {
    int? picked;
    await tester.pumpWidget(_pump(4, (v) => picked = v));

    await tester.tap(find.byType(InkWell));
    await tester.pumpAndSettle();

    expect(find.byType(SegmentedButton<String>), findsOneWidget);
    expect(find.text('Direction'), findsOneWidget);
    expect(find.text('Feel'), findsOneWidget);
    expect(find.text('In'), findsOneWidget);
    expect(find.text('Out'), findsOneWidget);
    expect(find.text('In-out'), findsOneWidget);
    expect(find.text('Linear'), findsOneWidget);
    expect(find.text('Smooth'), findsOneWidget);
    expect(find.text('Float'), findsOneWidget);
    expect(find.text('Random'), findsOneWidget);

    await tester.tap(find.text('Out'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Smooth'));
    await tester.pumpAndSettle();

    expect(picked, 3);
  });

  testWidgets(
      'a singleton family (Float) resolves to its sole value regardless of Direction',
      (tester) async {
    int? picked;
    await tester.pumpWidget(_pump(4, (v) => picked = v));

    await tester.tap(find.byType(InkWell));
    await tester.pumpAndSettle();

    await tester.tap(find.text('In'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Float'));
    await tester.pumpAndSettle();

    expect(picked, 2);
  });

  testWidgets('Random tile shows a shuffle glyph, not a sparkline',
      (tester) async {
    await tester.pumpWidget(_pump(4, (_) {}));

    await tester.tap(find.byType(InkWell));
    await tester.pumpAndSettle();

    expect(find.byIcon(Icons.shuffle), findsOneWidget);
    final sparklines = tester
        .widgetList<CustomPaint>(find.byType(CustomPaint))
        .where((cp) => cp.painter is EasingSparkline)
        .map((cp) => cp.painter as EasingSparkline)
        .toList();
    expect(sparklines, hasLength(3));
    expect(sparklines.map((s) => s.value), isNot(contains(kRandomEasingValue)));
  });

  testWidgets(
      'Motion grid is a fixed 4-wide layout showing all 8 catalog families',
      (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: MotionPicker(
          label: 'Motion',
          options: _fullOptions,
          value: 4,
          onChanged: (_) {},
        ),
      ),
    ));

    await tester.tap(find.byType(InkWell));
    await tester.pumpAndSettle();

    const row1 = ['Linear', 'Smooth', 'Snap', 'Float'];
    const row2 = ['Overshoot', 'Spring', 'Bounce', 'Random'];
    for (final label in [...row1, ...row2]) {
      expect(find.text(label), findsOneWidget);
    }

    // Same-row tiles land within a pixel of each other (the selected tile's
    // thicker border shifts its content by ~1px); rows themselves sit a full
    // tile height apart.
    final row1Y = row1.map((l) => tester.getTopLeft(find.text(l)).dy).toList();
    final row2Y = row2.map((l) => tester.getTopLeft(find.text(l)).dy).toList();
    for (final y in row1Y) {
      expect(y, closeTo(row1Y.first, 2));
    }
    for (final y in row2Y) {
      expect(y, closeTo(row2Y.first, 2));
    }
    expect((row2Y.first - row1Y.first).abs(), greaterThan(10));
  });

  testWidgets('a legacy catalog with no group still renders without crashing',
      (tester) async {
    const legacy = [
      EnumOption(value: 0, label: 'Linear'),
      EnumOption(value: 1, label: 'Smooth'),
    ];
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: MotionPicker(
          label: 'Motion',
          options: legacy,
          value: 0,
          onChanged: (_) {},
        ),
      ),
    ));

    await tester.tap(find.byType(InkWell));
    await tester.pumpAndSettle();

    expect(find.byType(SegmentedButton<String>), findsNothing);
    // Value 0 ('Linear') is both the trigger-row selection and a tile.
    expect(find.text('Linear'), findsNWidgets(2));
    expect(find.text('Smooth'), findsOneWidget);
  });
}
