import 'package:flutter/material.dart';

import '../../domain/lamp_color.dart';

/// A swatch that visualizes a [LampColor] — including the separate
/// warm-white LED's contribution. Renders as a stack of two layers
/// matching the original Vue ColorPreview component: a base RGB
/// layer with a warm-white tint (#FABB3E) overlaid at
/// `opacity = (W/255) * (availableRoom/765)`.
///
/// On 2026-06-12 this was briefly changed to a single Container using
/// the screen-blend math from [LampColor.blendedRgb] (which is correct
/// for gradient / SVG fills where you can't stack layers), but bright
/// colors with high W didn't wash visibly enough — the screen blend
/// preserves base brightness while alpha overlay actually mutes it
/// toward the warm tint. Operator feedback 2026-06-13 reverted this
/// widget back to the stacked alpha overlay. The screen-blend path
/// remains the right tool for [LampColor.toRgbHex] / gradient stops.

/// Two-shape variants of [LampColorSwatch]. The default is [circle] so
/// existing call-sites stay unchanged; the [roundedSquare] variant is
/// used by [ShadeCard] to visually rhyme with [BaseCard].
enum LampSwatchShape { circle, roundedSquare }

class LampColorSwatch extends StatelessWidget {
  const LampColorSwatch({
    super.key,
    required this.color,
    this.size = 48,
    this.borderColor,
    this.shape = LampSwatchShape.circle,
    this.borderRadius = 14,
  });

  final LampColor color;
  final double size;
  final Color? borderColor;
  final LampSwatchShape shape;
  final double borderRadius;

  /// `(W / 255) * (availableRoom / 765)`, clamped to [0, 1].
  /// Exposed for unit tests.
  static double warmWhiteOpacity(LampColor c) {
    final room = 765 - (c.r + c.g + c.b);
    if (room <= 0) return 0;
    final wwPct = c.w / 255.0;
    final roomPct = room / 765.0;
    return (wwPct * roomPct).clamp(0.0, 1.0);
  }

  @override
  Widget build(BuildContext context) {
    final isCircle = shape == LampSwatchShape.circle;
    final baseRgb = Color.fromARGB(0xFF, color.r, color.g, color.b);
    // Same warm tint as the Vue ColorPreview component (#FABB3E).
    const warmTint = Color(0xFFFABB3E);
    final overlayOpacity = warmWhiteOpacity(color);

    BoxDecoration decoration({required Color fill, bool drawBorder = false}) {
      return BoxDecoration(
        color: fill,
        shape: isCircle ? BoxShape.circle : BoxShape.rectangle,
        borderRadius:
            isCircle ? null : BorderRadius.circular(borderRadius),
        border: drawBorder
            ? Border.all(
                color: borderColor ?? Colors.white.withValues(alpha: 0.12),
              )
            : null,
      );
    }

    return SizedBox(
      width: size,
      height: size,
      child: Stack(
        children: [
          // Base RGB.
          Positioned.fill(
            child: Container(decoration: decoration(fill: baseRgb)),
          ),
          // Warm-white overlay, alpha-blended via [Opacity] so it
          // actually tints the base toward the warm tone (not just
          // adds glow on top of it).
          if (overlayOpacity > 0)
            Positioned.fill(
              child: Opacity(
                opacity: overlayOpacity,
                child: Container(decoration: decoration(fill: warmTint)),
              ),
            ),
          // Border drawn last so neither layer's edge clips it.
          Positioned.fill(
            child: Container(
              decoration: decoration(
                fill: Colors.transparent,
                drawBorder: true,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
