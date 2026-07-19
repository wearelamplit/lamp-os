import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../domain/lamp_color.dart';

/// Full-width bar split into equal hard-edged blocks, one per color, with no
/// blending between them. Only the bar's outer left/right corners are rounded;
/// internal block boundaries stay square. A trailing palette icon marks it as
/// tappable; tapping the bar opens its editor.
class ColorBlocksBar extends StatelessWidget {
  const ColorBlocksBar({super.key, required this.colors, this.onTap});

  final List<LampColor> colors;
  final VoidCallback? onTap;

  static const double _height = 44; // deliberate dimension, not spacing
  static const double _iconCell = 44; // deliberate dimension, not spacing

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final radius = BorderRadius.circular(AppRadius.swatch);
    return InkWell(
      onTap: onTap,
      borderRadius: radius,
      child: Container(
        height: _height,
        clipBehavior: Clip.antiAlias,
        decoration: BoxDecoration(
          color: cs.surfaceContainerHighest,
          borderRadius: radius,
          border: Border.all(
            color: cs.outlineVariant,
            width: 1, // deliberate dimension, not spacing
          ),
        ),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Expanded(
              child: colors.isEmpty
                  ? Center(
                      child: Text(
                        'Tap to set colors',
                        style: Theme.of(context).textTheme.bodySmall?.copyWith(
                              color: cs.onSurfaceVariant,
                            ),
                      ),
                    )
                  : Row(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        for (final c in colors)
                          Expanded(child: ColoredBox(color: c.toSwatch())),
                      ],
                    ),
            ),
            SizedBox(
              width: _iconCell,
              child: Center(
                child: Icon(Icons.palette_outlined, color: cs.onSurfaceVariant),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
