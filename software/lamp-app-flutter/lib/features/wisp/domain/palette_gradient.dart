// Palette to ramp math for the wisp's on-screen indicator bar.
//
// Mirrors `software/wisp/src/StatusRing.h`: same Q16.16 fractional-stop
// math, warm-white fallback, and RGBW->RGB warm bias. The bar and the wisp's
// ring MUST render a palette identically, or the bar stops being a useful
// indicator. Don't use Flutter's `LinearGradient` here: its rounding and
// endpoint handling diverge from the integer pipeline. The input palette is
// N equally-spaced stops stretched across `pixelCount` pixels with linear
// interpolation; N==0 falls back to warm-white, N==1 is flat.

import 'dart:ui' show Color;

/// Warm-white fallback when the gradient has no palette (Off, or empty
/// Manual/Aurora). Same R/G/B as `kWarmWhiteR/G/B` in `StatusRing.h`, tuned
/// to read as a tungsten bulb on NEO_GRB. Defined here (not BrandColors) so
/// the math mirrors the LED ring, not the app's editorial palette.
const Color kWarmWhite = Color.fromARGB(0xFF, 255, 180, 100);

/// Fold an Aurora RGBW sample to RGB for the on-screen bar (no W channel).
/// W biases warm (most to R, some to G, none to B) so a mostly-white palette
/// still reads warm. Channels clamp to 255. Mirrors `rgbwToRgbWarmBias` in
/// `StatusRing.h`: 0.7*W to R, 0.4*W to G; integer /10 keeps it identical.
Color foldRgbwWarmBias(int r, int g, int b, int w) {
  final addR = (w * 7) ~/ 10;
  final addG = (w * 4) ~/ 10;
  final outR = (r + addR).clamp(0, 255);
  final outG = (g + addG).clamp(0, 255);
  return Color.fromARGB(0xFF, outR, outG, b);
}

/// Render `stops` (RGB-only [Color]s, alpha ignored) into `pixelCount`
/// interpolated colors, left to right. Empty -> all [kWarmWhite]; single ->
/// flat; 2+ -> linear interpolation with stops on 0, 1/(N-1) ... 1.0. Mirrors
/// `computeRingGradient` in `StatusRing.h` (Q16.16 fixed-point, integer lerp)
/// so the bar matches the ring within <=1 LSB.
List<Color> renderPaletteRamp(List<Color> stops, int pixelCount) {
  if (pixelCount <= 0) return const <Color>[];
  if (stops.isEmpty) {
    return List<Color>.filled(pixelCount, kWarmWhite);
  }
  if (stops.length == 1) {
    return List<Color>.filled(pixelCount, stops[0]);
  }

  // numStops >= 2. Match StatusRing's Q16.16 pipeline so any rounding
  // happens identically on both sides.
  final n = stops.length;
  final denom = pixelCount - 1;
  final span = n - 1;
  // denom == 0 (single-pixel output) would divide by zero; collapse to the
  // first stop. StatusRing's caller never sends pixelCount==1.
  if (denom == 0) {
    return <Color>[stops[0]];
  }

  // Pre-extract channel bytes so the hot loop avoids repeated [Color]
  // shifts/masks.
  final rs = List<int>.filled(n, 0);
  final gs = List<int>.filled(n, 0);
  final bs = List<int>.filled(n, 0);
  for (var k = 0; k < n; k++) {
    final c = stops[k];
    rs[k] = (c.r * 255).round();
    gs[k] = (c.g * 255).round();
    bs[k] = (c.b * 255).round();
  }

  final out = List<Color>.filled(pixelCount, kWarmWhite);
  for (var i = 0; i < pixelCount; i++) {
    // t_q16 = (i * span * 65536) / denom — fractional palette-index in Q16.16.
    final tQ16 = (i * span * 65536) ~/ denom;
    final lo = tQ16 >> 16; // floor stop index
    var hi = lo + 1; // ceil stop index
    if (hi > span) hi = span; // clamp the trailing edge
    final frac = tQ16 & 0xFFFF; // 0..65535 weight on hi
    final invFrac = 65536 - frac;

    final r = (rs[lo] * invFrac + rs[hi] * frac + 32768) >> 16;
    final g = (gs[lo] * invFrac + gs[hi] * frac + 32768) >> 16;
    final b = (bs[lo] * invFrac + bs[hi] * frac + 32768) >> 16;
    out[i] = Color.fromARGB(0xFF, r, g, b);
  }
  return out;
}
