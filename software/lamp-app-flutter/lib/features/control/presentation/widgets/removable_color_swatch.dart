import 'package:flutter/material.dart';

import '../../domain/lamp_color.dart';
import 'lamp_color_swatch.dart';

/// Color swatch with an edit tap and an optional corner remove button.
/// The remove button sits top-right inside the widget's bounds; the swatch
/// occupies bottom-left. Both hit targets are fully contained so neither
/// steals taps from the other. `onRemove == null` hides the button
/// (e.g. a required last swatch).
class RemovableColorSwatch extends StatelessWidget {
  const RemovableColorSwatch({
    super.key,
    required this.color,
    required this.onEdit,
    this.onRemove,
    this.size = 40,
  });

  final LampColor color;
  final VoidCallback onEdit;
  final VoidCallback? onRemove;
  final double size;

  static const double _dot = 24; // deliberate dimension, not spacing
  static const double _target = 28; // deliberate dimension, not spacing
  static const double _margin = 15; // deliberate dimension, not spacing

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final hasRemove = onRemove != null;
    // Reserve the remove-button margin unconditionally so a swatch keeps the
    // same footprint whether or not its X is shown; adding/removing a color
    // then doesn't shift its neighbours in the surrounding Wrap.
    final extent = size + _margin;
    return SizedBox.square(
      dimension: extent,
      child: Stack(
        children: [
          if (hasRemove)
            Positioned(
              top: 0,
              right: 0,
              child: Semantics(
                label: 'Remove color',
                button: true,
                child: GestureDetector(
                  onTap: onRemove,
                  behavior: HitTestBehavior.opaque,
                  child: SizedBox.square(
                    dimension: _target,
                    child: Center(
                      child: SizedBox.square(
                        dimension: _dot,
                        child: Container(
                          decoration: BoxDecoration(
                            color: colorScheme.surfaceContainerHighest,
                            shape: BoxShape.circle,
                            border: Border.all(color: colorScheme.outline),
                          ),
                          child: Icon(
                            Icons.close,
                            size: 14, // deliberate dimension, not spacing
                            color: colorScheme.onSurface,
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ),
          // Swatch is last so it wins hit-tests in the overlap zone at the widget centre.
          Positioned(
            left: 0,
            bottom: 0,
            child: GestureDetector(
              onTap: onEdit,
              behavior: HitTestBehavior.opaque,
              child: LampColorSwatch(color: color, size: size),
            ),
          ),
        ],
      ),
    );
  }
}
