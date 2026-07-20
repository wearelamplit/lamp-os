import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/interval_range_slider.dart';

void main() {
  testWidgets('renders a RangeSlider', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: IntervalRangeSlider(
          values: const RangeValues(2, 8),
          min: 0,
          max: 30,
          onChanged: (_) {},
        ),
      ),
    ));
    expect(find.byType(RangeSlider), findsOneWidget);
  });

  testWidgets('onChanged fires with start <= end on thumb drag', (tester) async {
    RangeValues? changed;
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: IntervalRangeSlider(
          values: const RangeValues(2, 8),
          min: 0,
          max: 30,
          onChanged: (v) => changed = v,
        ),
      ),
    ));

    final slider = find.byType(RangeSlider);
    final rect = tester.getRect(slider);
    // Drag from near the start thumb (value=2 at ~9% of track).
    // Flutter RangeSlider reserves 24dp on each side for thumb hit areas.
    const trackPad = 24.0;
    final trackWidth = rect.width - 2 * trackPad;
    final startThumbX = rect.left + trackPad + trackWidth * 2 / 30;
    await tester.dragFrom(Offset(startThumbX, rect.center.dy), const Offset(30, 0));
    await tester.pump();

    expect(changed, isNotNull);
    expect(changed!.start, lessThanOrEqualTo(changed!.end));
  });

  testWidgets('minGap keeps the thumbs apart when dragged together',
      (tester) async {
    RangeValues? changed;
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: IntervalRangeSlider(
          values: const RangeValues(2, 8),
          min: 0,
          max: 30,
          minGap: 5,
          onChanged: (v) => changed = v,
        ),
      ),
    ));

    final slider = find.byType(RangeSlider);
    final rect = tester.getRect(slider);
    const trackPad = 24.0;
    final trackWidth = rect.width - 2 * trackPad;
    final startThumbX = rect.left + trackPad + trackWidth * 2 / 30;
    // Drag the low thumb far to the right, toward/past the high thumb.
    await tester.dragFrom(
        Offset(startThumbX, rect.center.dy), const Offset(200, 0));
    await tester.pump();

    expect(changed, isNotNull);
    expect(changed!.end - changed!.start, greaterThanOrEqualTo(5.0));
  });

  testWidgets('minGap clamps an initial pair that is too narrow',
      (tester) async {
    final captured = <RangeValues>[];
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: IntervalRangeSlider(
          values: const RangeValues(10, 11),
          min: 0,
          max: 30,
          minGap: 6,
          onChanged: captured.add,
        ),
      ),
    ));

    final RangeSlider widget = tester.widget(find.byType(RangeSlider));
    expect(widget.values.end - widget.values.start, greaterThanOrEqualTo(6.0));
  });
}
