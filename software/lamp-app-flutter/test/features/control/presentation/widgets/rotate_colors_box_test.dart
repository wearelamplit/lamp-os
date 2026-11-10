import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/utils/duration_format.dart';
import 'package:lamp_app/features/control/domain/color_rotation.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/rotate_colors_box.dart';
import 'package:lamp_app/features/control/presentation/widgets/rotating_gradient.dart';

const _two = [
  LampColor(r: 0xFF, g: 0, b: 0, w: 0),
  LampColor(r: 0, g: 0xFF, b: 0, w: 0),
];
const _one = [LampColor(r: 0xFF, g: 0, b: 0, w: 0)];

ColorRotation _rot({bool on = true, int periodS = 40, RotationDirection dir = RotationDirection.up}) =>
    ColorRotation(on: on, periodS: periodS, direction: dir);

Future<List<ColorRotation>> _pump(
  WidgetTester tester, {
  required ColorRotation rotation,
  List<LampColor> colors = _two,
}) async {
  final changes = <ColorRotation>[];
  await tester.pumpWidget(MaterialApp(
    theme: appTheme,
    home: Scaffold(
      body: SingleChildScrollView(
        child: RotateColorsBox(
          rotation: rotation,
          colors: colors,
          stop0AtBottom: false,
          onChanged: changes.add,
        ),
      ),
    ),
  ));
  await tester.pump();
  return changes;
}

Slider _slider(WidgetTester tester) => tester.widget<Slider>(find.byType(Slider));

void main() {
  group('Loop length slider', () {
    testWidgets('"slow" is left of the track and "fast" right', (tester) async {
      await _pump(tester, rotation: _rot());
      expect(find.text('Loop length'), findsOneWidget);
      expect(find.text('How long one full trip through your colors takes'),
          findsOneWidget);
      final track = tester.getCenter(find.byType(Slider)).dx;
      expect(tester.getCenter(find.text('slow')).dx, lessThan(track));
      expect(tester.getCenter(find.text('fast')).dx, greaterThan(track));
    });

    testWidgets('the left end sets 120 s and the right end 5 s', (tester) async {
      final changes = await _pump(tester, rotation: _rot());
      final s = _slider(tester);
      s.onChanged!(s.min);
      s.onChanged!(s.max);
      expect(changes.map((r) => r.periodS), [120, 5]);
    });

    testWidgets('dragging to the far left reads 120 s, far right 5 s',
        (tester) async {
      final changes = await _pump(tester, rotation: _rot());
      await tester.drag(find.byType(Slider), const Offset(-1000, 0));
      await tester.pump();
      expect(changes.last.periodS, 120);
      await tester.drag(find.byType(Slider), const Offset(1000, 0));
      await tester.pump();
      expect(changes.last.periodS, 5);
    });

    testWidgets('the middle of the track reads 62 or 63 s (linear in period)',
        (tester) async {
      final changes = await _pump(tester, rotation: _rot());
      final s = _slider(tester);
      s.onChanged!((s.min + s.max) / 2);
      expect(changes.single.periodS, anyOf(62, 63));
    });

    testWidgets('every whole second from 5 to 120 is reachable', (tester) async {
      final changes = await _pump(tester, rotation: _rot());
      final s = _slider(tester);
      expect(s.divisions, (s.max - s.min).round());
      for (var v = s.min; v <= s.max; v += 1) {
        s.onChanged!(v);
      }
      expect(changes.map((r) => r.periodS).toSet(),
          {for (var p = 5; p <= 120; p++) p});
    });

    for (final (period, shown) in [(5, '5s'), (63, '1m 3s'), (120, '2m')]) {
      testWidgets('reads $period s as "$shown" (formatDuration)', (tester) async {
        await _pump(tester, rotation: _rot(periodS: period));
        expect(shown, formatDuration(Duration(seconds: period)));
        expect(find.text(shown), findsOneWidget);
      });
    }

    testWidgets('the thumb sits at the inverted position of the value',
        (tester) async {
      await _pump(tester, rotation: _rot(periodS: 120));
      expect(_slider(tester).value, _slider(tester).min);
      await _pump(tester, rotation: _rot(periodS: 5));
      expect(_slider(tester).value, _slider(tester).max);
    });
  });

  group('switch and greyed state', () {
    testWidgets('the switch toggles on', (tester) async {
      final changes = await _pump(tester, rotation: _rot(on: false));
      expect(find.byType(Slider), findsNothing);
      await tester.tap(find.byType(Switch));
      await tester.pump();
      expect(changes.single.on, isTrue);
    });

    for (final on in [true, false]) {
      testWidgets('one colour (stored on: $on): greyed, hint, setting kept',
          (tester) async {
        final changes = await _pump(tester, rotation: _rot(on: on), colors: _one);
        expect(find.text('Add a second color to rotate.'), findsOneWidget);
        final sw = tester.widget<Switch>(find.byType(Switch));
        expect(sw.onChanged, isNull);
        expect(sw.value, on);
        expect(find.byType(Slider), findsNothing);
        expect(find.byType(RotatingGradient), findsNothing);
        expect(find.text('Direction'), findsNothing);
        await tester.tap(find.byType(Switch));
        await tester.pump();
        expect(changes, isEmpty);
      });
    }

    testWidgets('two colours: the hint gives way to the subtitle', (tester) async {
      await _pump(tester, rotation: _rot());
      expect(find.text('Add a second color to rotate.'), findsNothing);
      expect(find.text('Slowly flow these colors along the strip'), findsOneWidget);
      expect(tester.widget<Switch>(find.byType(Switch)).onChanged, isNotNull);
    });
  });

  group('Direction', () {
    testWidgets('shows only while Rotate is on', (tester) async {
      await _pump(tester, rotation: _rot(on: false));
      expect(find.text('Direction'), findsNothing);
      await _pump(tester, rotation: _rot());
      expect(find.text('Direction'), findsOneWidget);
      expect(find.text('Up rises toward the top of the lamp. Down falls.'),
          findsOneWidget);
    });

    testWidgets('sits directly under the Loop length slider', (tester) async {
      await _pump(tester, rotation: _rot());
      expect(tester.getTopLeft(find.text('Direction')).dy,
          greaterThan(tester.getBottomLeft(find.byType(Slider)).dy));
    });

    for (final dir in RotationDirection.values) {
      testWidgets('shows ${dir.wire} as selected, a tap on the other reaches '
          'onChanged', (tester) async {
        final changes = await _pump(tester, rotation: _rot(dir: dir));
        final seg = tester.widget<SegmentedButton<int>>(
            find.byType(SegmentedButton<int>));
        expect(seg.selected, {dir == RotationDirection.up ? 0 : 1});
        final other = dir == RotationDirection.up ? 'Down' : 'Up';
        await tester.tap(find.text(other));
        await tester.pump();
        expect(changes.single.direction, isNot(dir));
        expect(changes.single.periodS, 40);
        expect(changes.single.on, isTrue);
      });
    }
  });
}
