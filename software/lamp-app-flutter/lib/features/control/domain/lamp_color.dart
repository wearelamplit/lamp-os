import 'dart:math' show max, min, sqrt;

import 'package:flutter/painting.dart' show Color;

/// A lamp color carries an explicit white channel alongside RGB. The firmware
/// serializes these as `#RRGGBBWW` hex strings on every section payload.
class LampColor {
  const LampColor({
    required this.r,
    required this.g,
    required this.b,
    required this.w,
  });

  final int r;
  final int g;
  final int b;
  final int w;

  /// Sentinel used in places like the shade preview that need a "no
  /// colors yet" baseline. Kept on `LampColor` so both
  /// `ControlNotifier` and `ControlScreen` can share the same instance.
  static const black = LampColor(r: 0, g: 0, b: 0, w: 0);

  /// Single representative color for a gradient, blended in linear light
  /// (de-gamma square, mean, re-gamma sqrt) with a chroma guard that keeps a
  /// complementary pair from collapsing to grey. Matches the firmware's
  /// `lampos::led::blendedIdentity`; keep the two in lockstep.
  static LampColor blendedIdentity(List<LampColor> stops) {
    if (stops.isEmpty) return black;
    if (stops.length == 1) return stops.first;
    const guard = 0.5;
    var rl = 0.0, gl = 0.0, bl = 0.0, wl = 0.0, inChroma = 0.0;
    for (final s in stops) {
      final r = s.r / 255, g = s.g / 255, b = s.b / 255, w = s.w / 255;
      rl += r * r;
      gl += g * g;
      bl += b * b;
      wl += w * w;
      inChroma += max(r, max(g, b)) - min(r, min(g, b));
    }
    final inv = 1 / stops.length;
    var r = sqrt(rl * inv), g = sqrt(gl * inv), b = sqrt(bl * inv);
    final w = sqrt(wl * inv);
    inChroma *= inv;
    final mx = max(r, max(g, b)), mn = min(r, min(g, b));
    final chroma = mx - mn, target = guard * inChroma;
    if (inChroma > 0 && chroma < target) {
      final mid = (mx + mn) / 2;
      var hr = r, hg = g, hb = b;
      if (chroma <= 1e-4) {
        hr = stops.first.r / 255;
        hg = stops.first.g / 255;
        hb = stops.first.b / 255;
      }
      final hmx = max(hr, max(hg, hb)), hmn = min(hr, min(hg, hb));
      final hc = hmx - hmn, hmid = (hmx + hmn) / 2;
      if (hc > 1e-4) {
        final k = target / hc;
        r = mid + (hr - hmid) * k;
        g = mid + (hg - hmid) * k;
        b = mid + (hb - hmid) * k;
      }
    }
    int q(double v) => (v.clamp(0.0, 1.0) * 255).round();
    return LampColor(r: q(r), g: q(g), b: q(b), w: q(w));
  }

  factory LampColor.fromHex(String input) {
    var s = input.startsWith('#') ? input.substring(1) : input;
    if (s.length != 8) {
      throw FormatException('LampColor.fromHex expects 8 hex chars, got "$input"');
    }
    return LampColor(
      r: int.parse(s.substring(0, 2), radix: 16),
      g: int.parse(s.substring(2, 4), radix: 16),
      b: int.parse(s.substring(4, 6), radix: 16),
      w: int.parse(s.substring(6, 8), radix: 16),
    );
  }

  /// Lenient parser used by the color-picker hex input. Accepts `#RRGGBB`
  /// (W defaults to 0) or `#RRGGBBWW`, leading `#` optional, case-insensitive.
  /// Returns null on any parse failure so the caller can surface an error
  /// state without a try/catch.
  static LampColor? tryFromHex(String input) {
    var s = input.trim();
    if (s.startsWith('#')) s = s.substring(1);
    if (s.length != 6 && s.length != 8) return null;
    final r = int.tryParse(s.substring(0, 2), radix: 16);
    final g = int.tryParse(s.substring(2, 4), radix: 16);
    final b = int.tryParse(s.substring(4, 6), radix: 16);
    if (r == null || g == null || b == null) return null;
    if (s.length == 6) return LampColor(r: r, g: g, b: b, w: 0);
    final w = int.tryParse(s.substring(6, 8), radix: 16);
    if (w == null) return null;
    return LampColor(r: r, g: g, b: b, w: w);
  }

  String toHex() {
    String h(int v) => v.toRadixString(16).padLeft(2, '0').toUpperCase();
    return '#${h(r)}${h(g)}${h(b)}${h(w)}';
  }

  /// Blend the warm-white channel into RGB for on-screen rendering.
  /// Composites a `#FABB3E` warm-white overlay onto the RGB layer using
  /// SCREEN blend (`1 - (1-a)(1-b)`), not alpha blend. Screen brightens
  /// additively, matching how the physical W LED adds light on top
  /// of the RGB LEDs. Alpha blend muddies bright colors toward the
  /// orange tint instead of brightening them.
  ///
  /// Algorithm:
  ///   - tint = #FABB3E
  ///   - opacity = (W / 255) × (headroom / 765)
  ///       where headroom = 765 − (R+G+B)
  ///   - per channel: result = 255 × (1 − (1 − c/255) × (1 − (tintC/255) × opacity))
  ///
  /// High RGB → no headroom, no warm overlay (LED clips anyway).
  /// Low RGB + high W → strong warm glow.
  /// Pure warm-white (W=255, RGB=0) → fully tinted swatch.
  ({int r, int g, int b}) get blendedRgb {
    // Warm-white sRGB approximation. Matches Brand.warmWhite (#FABB3E).
    // Hardcoded here so the domain layer doesn't depend on theme.
    const tintR = 0xFA;
    const tintG = 0xBB;
    const tintB = 0x3E;
    final headroom = 765 - (r + g + b);
    if (headroom <= 0 || w == 0) return (r: r, g: g, b: b);
    final opacity = (w / 255) * (headroom / 765);
    // Screen blend: result = 1 - (1 - base) * (1 - overlay·α)
    int screen(int base, int tint) {
      final a = base / 255.0;
      final b = (tint / 255.0) * opacity;
      return (255 * (1 - (1 - a) * (1 - b))).round().clamp(0, 255);
    }
    return (
      r: screen(r, tintR),
      g: screen(g, tintG),
      b: screen(b, tintB),
    );
  }

  /// Flutter [Color] with the warm-white channel blended in.
  /// Phone screens can't reproduce the physical W LED directly, so
  /// this approximates it with a warm-tone overlay on RGB.
  Color toSwatch() {
    final c = blendedRgb;
    return Color.fromARGB(0xFF, c.r, c.g, c.b);
  }

  /// `RRGGBB` hex (no leading `#`) of the W-blended color. Used by
  /// the SVG color substitution in LampPreview.
  String toRgbHex() {
    String h(int v) => v.toRadixString(16).padLeft(2, '0').toUpperCase();
    final c = blendedRgb;
    return '${h(c.r)}${h(c.g)}${h(c.b)}';
  }

  LampColor withRgb({required int r, required int g, required int b}) =>
      LampColor(r: r, g: g, b: b, w: w);

  @override
  bool operator ==(Object other) =>
      other is LampColor &&
      other.r == r &&
      other.g == g &&
      other.b == b &&
      other.w == w;

  @override
  int get hashCode => Object.hash(r, g, b, w);

  @override
  String toString() => 'LampColor(${toHex()})';
}
