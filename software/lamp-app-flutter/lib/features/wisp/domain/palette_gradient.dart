// Palette → ramp math for the wisp's on-screen indicator bar.
//
// Mirrors `software/wisp/src/StatusRing.h` byte-for-byte: same Q16.16
// fractional-stop math, same warm-white fallback, same RGBW→RGB warm
// bias. The two surfaces (Flutter UI bar, NEO_GRB ring on the wisp)
// MUST stay in lockstep — if a 5-stop palette renders one way on the
// LEDs and another way in the app, the gradient bar stops being a
// useful indicator. Don't reach for Flutter's built-in `LinearGradient`
// here; the rounding and endpoint handling subtly diverge from the
// integer pipeline that runs on the wisp.
//
// Layout: the input palette is treated as a series of equally-spaced
// color stops which we stretch across `pixelCount` output pixels with
// linear interpolation between adjacent stops. With N stops:
//   - N == 0: caller falls back to warm-white (renderPaletteRamp
//     handles this internally — every pixel is [kWarmWhite]).
//   - N == 1: every pixel takes that single color.
//   - N >= 2: pixel i maps to fractional stop position
//             t = (i / (pixelCount - 1)) * (N - 1)
//             with lerp between floor(t) and ceil(t) stops.
//
// No Flutter dependencies beyond `dart:ui`'s [Color] — safe to test in
// pure-Dart unit tests.

import 'dart:ui' show Color;

/// Warm-white fallback used when the gradient has no palette to display
/// (sourceMode=Off, or Manual/Aurora with an empty palette). Same R/G/B
/// values as `kWarmWhiteR/G/B` in `StatusRing.h` — tuned to read as a
/// tungsten bulb on a NEO_GRB WS2812 strip, slightly amber, no blue
/// bias. Defined here (not pulled from Brand.warmWhite, #FABB3E) so the
/// math stays decoupled from theme — the indicator is meant to mirror
/// what the LED ring is actually showing, not the app's editorial palette.
const Color kWarmWhite = Color.fromARGB(0xFF, 255, 180, 100);

/// Fold an Aurora RGBW sample down to RGB for the on-screen bar (which
/// has no W channel). The W contribution biases warm — most of it lands
/// on R, some on G, almost none on B — so a palette that's mostly
/// "white" still reads as warm rather than washed-out. Each channel is
/// clamped to 255.
///
/// Mirrors `rgbwToRgbWarmBias` in `StatusRing.h`: 0.7·W → R, 0.4·W → G,
/// W → 0 on B. Integer math via /10 keeps the result identical to the
/// firmware's pipeline.
Color foldRgbwWarmBias(int r, int g, int b, int w) {
  final addR = (w * 7) ~/ 10;
  final addG = (w * 4) ~/ 10;
  final outR = (r + addR).clamp(0, 255);
  final outG = (g + addG).clamp(0, 255);
  return Color.fromARGB(0xFF, outR, outG, b);
}

/// Render `stops` (RGB-only [Color]s, alpha ignored) into a list of
/// `pixelCount` interpolated colors, left-to-right.
///
/// Empty stops → every pixel is [kWarmWhite]. Single stop → every
/// pixel is that color. Two or more stops → linear interpolation with
/// stops landing exactly on positions 0, 1/(N-1), 2/(N-1), … 1.0.
///
/// The implementation mirrors `computeRingGradient` in `StatusRing.h`:
/// Q16.16 fixed-point fractional-stop index, integer lerp per channel,
/// rounded by adding 32768 before the >> 16. The two pipelines produce
/// matching pixels (within ≤1 LSB rounding) so the on-screen bar reads
/// as a true "what's the ring doing right now?" indicator.
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
  // Single-pixel output is a degenerate case (denom == 0). We treat it
  // as "everything collapses to the first stop" rather than divide by
  // zero — StatusRing's caller never sends pixelCount==1 so this is a
  // belt-and-braces guard.
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
