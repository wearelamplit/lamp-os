import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/theme/brand.dart';
import '../../domain/lamp_color.dart';

/// A swatch that visualizes a [LampColor], including the separate
/// warm-white LED's contribution. Renders as a stack of two layers: a base
/// RGB layer with a warm-white tint (#FABB3E) overlaid at
/// `opacity = (W/255) * (availableRoom/765)`.
///
/// Uses a stacked alpha overlay rather than [LampColor.blendedRgb]'s screen
/// blend: screen blend preserves base brightness, while alpha overlay mutes
/// it toward the warm tint, giving bright high-W colors a visible wash. The
/// screen-blend path remains the right tool for [LampColor.toRgbHex] /
/// gradient stops, where layers can't be stacked.

/// Two-shape variants of [LampColorSwatch]. The default is [roundedSquare]
/// so every color swatch reads as a rounded square; [circle] stays for the
/// rare caller that needs it.
enum LampSwatchShape { circle, roundedSquare }

class LampColorSwatch extends StatelessWidget {
  const LampColorSwatch({
    super.key,
    required this.color,
    this.size = 48,
    this.borderColor,
    this.shape = LampSwatchShape.roundedSquare,
    this.borderRadius = AppRadius.swatch,
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
    const warmTint = Brand.warmWhite;
    final overlayOpacity = warmWhiteOpacity(color);
    final defaultBorder =
        Theme.of(context).colorScheme.outlineVariant;

    BoxDecoration decoration({required Color fill, bool drawBorder = false}) {
      return BoxDecoration(
        color: fill,
        shape: isCircle ? BoxShape.circle : BoxShape.rectangle,
        borderRadius:
            isCircle ? null : BorderRadius.circular(borderRadius),
        border: drawBorder
            ? Border.all(color: borderColor ?? defaultBorder)
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
