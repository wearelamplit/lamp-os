import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/color_rotation.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/rotating_gradient.dart';

const _rgb = [
  LampColor(r: 0xFF, g: 0, b: 0, w: 0),
  LampColor(r: 0, g: 0xFF, b: 0, w: 0),
  LampColor(r: 0, g: 0, b: 0xFF, w: 0),
];
const _period = 40;
const _boundary = Key('boundary');

enum _Hue { red, green, blue }

Future<void> _pump(
  WidgetTester tester, {
  required bool stop0AtBottom,
  required RotationDirection direction,
  bool reduceMotion = false,
}) =>
    tester.pumpWidget(MediaQuery(
      data: MediaQueryData(disableAnimations: reduceMotion),
      child: Center(
        child: RepaintBoundary(
          key: _boundary,
          child: SizedBox(
            width: 12,
            height: 90,
            child: RotatingGradient(
              colors: _rgb,
              rotation: ColorRotation(
                  on: true, periodS: _period, direction: direction),
              stop0AtBottom: stop0AtBottom,
            ),
          ),
        ),
      ),
    ));

/// The dominant hue of the pixel row at the top or bottom edge, or [inset]
/// of the height in from it.
Future<_Hue> _edge(WidgetTester tester,
    {required bool bottom, double inset = 0}) async {
  final boundary =
      tester.renderObject<RenderRepaintBoundary>(find.byKey(_boundary));
  final bytes = await tester.runAsync(() async {
    final image = await boundary.toImage();
    final data = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
    final fromEdge = (inset * (image.height - 1)).round();
    final row = bottom ? image.height - 1 - fromEdge : fromEdge;
    final at = (row * image.width + image.width ~/ 2) * 4;
    final px = data!.buffer.asUint8List(at, 3);
    image.dispose();
    return px;
  });
  final (r, g, b) = (bytes![0], bytes[1], bytes[2]);
  if (r > g && r > b) return _Hue.red;
  if (g > r && g > b) return _Hue.green;
  return _Hue.blue;
}

/// The hue at the stop-0 edge at 0, a third and two thirds of a loop.
Future<List<_Hue>> _sequence(
  WidgetTester tester, {
  required bool stop0AtBottom,
  required RotationDirection direction,
}) async {
  await _pump(tester, stop0AtBottom: stop0AtBottom, direction: direction);
  // The ticker's first frame is its zero.
  await tester.pump();
  const third = Duration(milliseconds: _period * 1000 ~/ 3);
  final out = [await _edge(tester, bottom: stop0AtBottom)];
  await tester.pump(third);
  out.add(await _edge(tester, bottom: stop0AtBottom));
  await tester.pump(third);
  out.add(await _edge(tester, bottom: stop0AtBottom));
  return out;
}

void main() {
  testWidgets('shade of a standard lamp (stop 0 at the bottom), Up', (tester) async {
    expect(
        await _sequence(tester,
            stop0AtBottom: true, direction: RotationDirection.up),
        [_Hue.red, _Hue.blue, _Hue.green]);
  });

  testWidgets('stop 0 at the bottom, Down reverses it', (tester) async {
    expect(
        await _sequence(tester,
            stop0AtBottom: true, direction: RotationDirection.down),
        [_Hue.red, _Hue.green, _Hue.blue]);
  });

  testWidgets('base (stop 0 at the top), Up', (tester) async {
    expect(
        await _sequence(tester,
            stop0AtBottom: false, direction: RotationDirection.up),
        [_Hue.red, _Hue.green, _Hue.blue]);
  });

  testWidgets('stop 0 at the top, Down reverses it', (tester) async {
    expect(
        await _sequence(tester,
            stop0AtBottom: false, direction: RotationDirection.down),
        [_Hue.red, _Hue.blue, _Hue.green]);
  });

  for (final stop0AtBottom in [true, false]) {
    testWidgets(
        'reduce-motion holds still with stop 0 at its edge '
        '(stop0AtBottom $stop0AtBottom)', (tester) async {
      await _pump(tester,
          stop0AtBottom: stop0AtBottom,
          direction: RotationDirection.up,
          reduceMotion: true);
      await tester.pump(const Duration(seconds: 13));
      expect(tester.binding.transientCallbackCount, 0);
      expect(await _edge(tester, bottom: stop0AtBottom), _Hue.red);
      expect(await _edge(tester, bottom: stop0AtBottom, inset: 1 / 3),
          _Hue.green);
    });
  }

  testWidgets('animates while motion is allowed', (tester) async {
    await _pump(tester, stop0AtBottom: false, direction: RotationDirection.up);
    expect(tester.binding.transientCallbackCount, greaterThan(0));
  });
}
