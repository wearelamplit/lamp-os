import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/utils/duration_format.dart';
import '../../domain/color_rotation.dart';
import '../../domain/lamp_color.dart';

/// The rotation a card shows as moving: the section offers it, it is on and
/// there are colours to cycle. Null when the card stays still.
ColorRotation? effectiveRotation(
  ColorRotation? rotation,
  List<LampColor> colors,
) =>
    rotation != null && rotation.on && colors.length >= 2 ? rotation : null;

/// A Base or Shade card's title, with its loop length under it ("Rotating ·
/// 40s loop") while [rotation] is non-null.
class CardTitle extends StatelessWidget {
  const CardTitle({super.key, required this.title, required this.rotation});

  final String title;
  final ColorRotation? rotation;

  @override
  Widget build(BuildContext context) {
    final text = Theme.of(context).textTheme;
    final r = rotation;
    final heading = Text(title, style: text.titleMedium);
    if (r == null) return heading;
    final accent = Theme.of(context).colorScheme.onPrimaryContainer;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        heading,
        const SizedBox(height: AppSpace.xs),
        Row(
          children: [
            Icon(
              Icons.refresh,
              size: 14, // deliberate dimension, not spacing
              color: accent,
            ),
            const SizedBox(width: AppSpace.sm),
            Flexible(
              child: Text(
                'Rotating · ${formatDuration(Duration(seconds: r.periodS))} loop',
                style: text.bodySmall?.copyWith(color: accent),
              ),
            ),
          ],
        ),
      ],
    );
  }
}
