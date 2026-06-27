import 'package:flutter/material.dart';
import '../../../core/theme/brand.dart';

/// Screen-blend the warm-white channel (intensity `w` 0..255) over [rgb],
/// so the preview shows the physical warm-white pixel's contribution.
Color blendWarmWhite(Color rgb, int w, {Color warmWhite = Brand.warmWhite}) {
  if (w <= 0) return rgb;
  final f = (w.clamp(0, 255)) / 255.0;
  int ch(int base, int top) {
    final t = (top * f).round();
    return 255 - ((255 - base) * (255 - t) ~/ 255);
  }
  return Color.fromARGB(255, ch(rgb.red, warmWhite.red),
      ch(rgb.green, warmWhite.green), ch(rgb.blue, warmWhite.blue));
}
