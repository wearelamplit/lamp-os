import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/features/lamp_shell/domain/expression_catalog.dart';
import 'package:lamp_app/features/lamp_shell/presentation/widgets/expression_params_panel.dart';

ExpressionDescriptor _motionCapable() => ExpressionDescriptor.fromJson({
      'id': 'breathing',
      'name': 'Breathing',
      'colors': {'max': 8},
      'params': [
        {
          'key': 'easing',
          'type': 'enum',
          'label': 'Motion',
          'min': 0,
          'max': 17,
          'step': 1,
          'default': 0,
          'options': [
            {'value': 0, 'label': 'Linear', 'group': 'Linear'},
          ],
        },
        {
          'key': 'opacity',
          'type': 'int',
          'label': 'Opacity',
          'min': 10,
          'max': 100,
          'step': 5,
          'default': 100,
          'unit': '%',
        },
      ],
    });

ExpressionDescriptor _withBehaviourAndTiming() => ExpressionDescriptor.fromJson({
      'id': 'spotty',
      'name': 'Spotty',
      'colors': {'max': 8},
      'interval': {'min': 60, 'max': 900, 'step': 30, 'unit': 's', 'default': [60, 900]},
      'params': [
        {
          'key': 'mode',
          'type': 'enum',
          'label': 'Mode',
          'min': 0,
          'max': 1,
          'step': 1,
          'default': 0,
          'options': [
            {'value': 0, 'label': 'Steady'},
            {'value': 1, 'label': 'Jitter'},
          ],
        },
      ],
    });

ExpressionDescriptor _glitchyLike() => ExpressionDescriptor.fromJson({
      'id': 'glitchy',
      'name': 'Glitchy',
      'colors': {'max': 8},
      'params': [
        {
          'key': 'opacity',
          'type': 'int',
          'label': 'Opacity',
          'min': 10,
          'max': 100,
          'step': 5,
          'default': 100,
          'unit': '%',
        },
      ],
    });

Widget _pump(ExpressionDescriptor descriptor) => MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: ExpressionParamsPanel(
          descriptor: descriptor,
          parameters: const {},
          onChanged: (_) {},
          pixelCount: 38,
          intervalMin: 60,
          intervalMax: 900,
          onIntervalChanged: (_, _) {},
          part: ExpressionPanelPart.placement,
        ),
      ),
    );

void main() {
  testWidgets('motion-capable descriptor shows Motion & Appearance with picker and opacity',
      (tester) async {
    await tester.pumpWidget(_pump(_motionCapable()));
    expect(find.text('MOTION & APPEARANCE'), findsOneWidget);
    expect(find.text('Motion'), findsOneWidget);
    expect(find.text('Opacity'), findsOneWidget);
    // part: placement never emits the Behaviour card, so Opacity landing
    // here (not dropped) confirms it's grouped into Motion & Appearance.
    expect(find.text('BEHAVIOUR'), findsNothing);
  });

  testWidgets('glitchy-like descriptor shows Motion & Appearance with only opacity',
      (tester) async {
    await tester.pumpWidget(_pump(_glitchyLike()));
    expect(find.text('MOTION & APPEARANCE'), findsOneWidget);
    expect(find.text('Opacity'), findsOneWidget);
    expect(find.text('Motion'), findsNothing);
  });

  testWidgets('Behaviour renders in the placement part, ahead of Timing in the main part',
      (tester) async {
    final descriptor = _withBehaviourAndTiming();
    Widget panel(ExpressionPanelPart part) => ExpressionParamsPanel(
          descriptor: descriptor,
          parameters: const {},
          onChanged: (_) {},
          pixelCount: 38,
          intervalMin: 60,
          intervalMax: 900,
          onIntervalChanged: (_, _) {},
          part: part,
        );
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: SingleChildScrollView(
          child: Column(
            children: [
              panel(ExpressionPanelPart.placement),
              panel(ExpressionPanelPart.main),
            ],
          ),
        ),
      ),
    ));

    expect(find.text('BEHAVIOUR'), findsOneWidget);
    expect(find.text('TIMING'), findsOneWidget);
    final behaviourY = tester.getTopLeft(find.text('BEHAVIOUR')).dy;
    final timingY = tester.getTopLeft(find.text('TIMING')).dy;
    expect(behaviourY, lessThan(timingY));
  });
}
