import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/utils/duration_format.dart';
import '../../../../core/widgets/param_controls.dart';
import '../../domain/color_rotation.dart';
import '../../domain/lamp_color.dart';
import 'rotating_gradient.dart';

/// The Rotate box in the Base and Shade colour sheets: a switch, and while
/// rotating, a moving preview, the Loop length slider and the direction.
/// Under two colours it greys out and keeps the stored setting.
class RotateColorsBox extends StatelessWidget {
  const RotateColorsBox({
    super.key,
    required this.rotation,
    required this.colors,
    required this.stop0AtBottom,
    required this.onChanged,
  });

  final ColorRotation rotation;
  final List<LampColor> colors;

  /// Which end of the preview holds the first colour; see
  /// [RotatingGradient.stop0AtBottom].
  final bool stop0AtBottom;
  final ValueChanged<ColorRotation> onChanged;

  static const _directions = [RotationDirection.up, RotationDirection.down];

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final text = Theme.of(context).textTheme;
    final canRotate = colors.length >= 2;
    final active = canRotate && rotation.on;
    final header = Row(
      children: [
        Icon(
          Icons.refresh,
          size: 22, // deliberate dimension, not spacing
          color: active ? cs.primary : cs.onSurfaceVariant,
        ),
        const SizedBox(width: AppSpace.md),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Rotate colors',
                style: text.titleSmall?.copyWith(
                  color: canRotate ? null : cs.onSurfaceVariant,
                ),
              ),
              Text(
                canRotate
                    ? 'Slowly flow these colors along the strip'
                    : 'Add a second color to rotate.',
                style: text.bodySmall?.copyWith(color: cs.onSurfaceVariant),
              ),
            ],
          ),
        ),
        Switch(
          value: rotation.on,
          onChanged: canRotate
              ? (on) => onChanged(rotation.copyWith(on: on))
              : null,
        ),
      ],
    );
    final controls = Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Container(
          width: 28, // deliberate dimension, not spacing
          height: 136, // deliberate dimension, not spacing
          foregroundDecoration: BoxDecoration(
            borderRadius: BorderRadius.circular(AppRadius.swatch),
            border: Border.all(
              color: cs.outlineVariant,
              width: 1, // deliberate dimension, not spacing
            ),
          ),
          child: RotatingGradient(
            colors: colors,
            rotation: rotation,
            stop0AtBottom: stop0AtBottom,
            borderRadius: BorderRadius.circular(AppRadius.swatch),
          ),
        ),
        const SizedBox(width: AppSpace.lg),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              ParamSlider(
                label: 'Loop length',
                help: 'How long one full trip through your colors takes',
                value: rotation.periodS,
                min: kRotationMinPeriodS,
                max: kRotationMaxPeriodS,
                invert: true,
                leftLabel: 'slow',
                rightLabel: 'fast',
                format: (v) => formatDuration(Duration(seconds: v)),
                onChanged: (v) => onChanged(rotation.copyWith(periodS: v)),
              ),
              const SizedBox(height: AppSpace.md),
              ParamSegmented(
                label: 'Direction',
                help: 'Up rises toward the top of the lamp. Down falls.',
                options: const ['Up', 'Down'],
                selectedIndex: _directions.indexOf(rotation.direction),
                onIndex: (i) =>
                    onChanged(rotation.copyWith(direction: _directions[i])),
              ),
            ],
          ),
        ),
      ],
    );
    return Container(
      padding: const EdgeInsets.symmetric(
          horizontal: AppSpace.lg, vertical: AppSpace.md),
      decoration: BoxDecoration(
        color: cs.surfaceContainerHigh,
        borderRadius: BorderRadius.circular(AppRadius.card),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          MergeSemantics(child: header),
          if (active) ...[
            const SizedBox(height: AppSpace.md),
            controls,
          ],
        ],
      ),
    );
  }
}
