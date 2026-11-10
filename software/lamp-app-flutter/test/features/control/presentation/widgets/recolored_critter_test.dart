import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/presentation/widgets/recolored_critter.dart';

import 'svg_gradient_probe.dart';

void main() {
  // Onboarding knows no lampType, so its critter keeps the asset's drawing:
  // stop 0 at the top of the shade.
  testWidgets('the shade keeps list order and the asset axis', (tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: Scaffold(
        body: RecoloredCritter(
          deviceId: 'dev-1',
          shadeColors: [
            LampColor(r: 0xFF, g: 0, b: 0, w: 0),
            LampColor(r: 0, g: 0xFF, b: 0, w: 0),
            LampColor(r: 0, g: 0, b: 0xFF, w: 0),
          ],
          baseColors: [LampColor(r: 0xFF, g: 0xFF, b: 0, w: 0)],
        ),
      ),
    ));
    await tester.pumpAndSettle();
    final svg = renderedSvg(tester);
    expect(gradientStops(svg, 'Shade'),
        ['0%:FF0000', '50%:00FF00', '100%:0000FF']);
    expect(gradientOpenTag(svg, 'Shade'), contains(kAssetShadeAxis));
  });
}
