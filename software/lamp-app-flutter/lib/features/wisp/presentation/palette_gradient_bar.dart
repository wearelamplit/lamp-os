// Full-width palette gradient bar shown at the very top of the wisp
// pane. Drives its colors through `renderPaletteRamp` so the on-screen
// gradient matches what the wisp's 30-pixel NeoPixel ring is showing
// pixel-for-pixel. See `domain/palette_gradient.dart` for the math.
//
// The Aurora source mode currently has no app-side palette: the wisp
// only publishes a paletteId prefix over BLE, not the stops themselves
// (carrying them would push the wispStatus JSON past CONTROL_MAX_PAYLOAD).
// So Aurora mode falls back to warm-white here, exactly like the LED
// ring does at boot before its first Aurora callback. Once we extend
// the wire format to carry stops (separate, future work),
// this widget will start rendering them automatically — no UI change
// needed beyond wiring the new state into `stops`.

import 'package:flutter/material.dart';

import '../../control/domain/lamp_color.dart';
import '../domain/palette_gradient.dart';
import '../domain/wisp_source_mode.dart';

/// Horizontal bar at the very top of the wisp pane. Stretches edge-to-
/// edge (no padding), 36 px tall by default — enough that the gradient
/// reads as a band rather than a hairline, low enough that it doesn't
/// crowd the header below.
///
/// Picks its stops from [sourceMode]:
///   - Off    → the operator-chosen [offColor] as a single solid color
///   - Manual → the manual palette draft (so edits preview live before
///              the operator hits Save)
///   - Aurora → warm-white fill until the app gets an Aurora palette
///              over the wire (currently never; see file-level comment)
class PaletteGradientBar extends StatelessWidget {
  const PaletteGradientBar({
    super.key,
    required this.sourceMode,
    required this.manualPalette,
    required this.offColor,
    this.height = 36,
    // 30 matches the wisp ring's pixel count; finer is a visual lie and
    // ~8x the per-repaint cost, and the eye can't resolve >30 samples at
    // 36 px height. A parameter so debug screens can crank it back up to
    // compare against the firmware ramp.
    this.pixelCount = 30,
  });

  /// Current wisp source mode. Drives which palette feeds the bar.
  final WispSourceMode sourceMode;

  /// Manual palette to render when [sourceMode] is
  /// [WispSourceMode.manual]. Ignored in Off/Aurora modes. We pass the
  /// editor draft (not the saved snapshot) so the bar updates live as
  /// the operator adds/edits swatches — same idea as the swatch row
  /// directly below it.
  final List<LampColor> manualPalette;

  /// Operator-chosen color rendered on the wisp's own 30-pixel ring
  /// while [sourceMode] is [WispSourceMode.off]. Mirrors the wisp-side
  /// `offColor` NVS field; previewed here as a single solid stop so the
  /// gradient bar at the top of the pane stays a faithful mirror of the
  /// ring across all three modes.
  final LampColor offColor;

  /// Vertical extent of the bar in logical pixels.
  final double height;

  /// Number of sample points across the bar. 256 keeps the gradient
  /// smooth on phone screens without churning through more colors than
  /// the eye can resolve at this height. The same math runs at the
  /// firmware's 30-pixel ring resolution.
  final int pixelCount;

  @override
  Widget build(BuildContext context) {
    final stops = _stopsFor(sourceMode, manualPalette, offColor);
    final ramp = renderPaletteRamp(stops, pixelCount);
    return SizedBox(
      width: double.infinity,
      height: height,
      child: CustomPaint(painter: _RampPainter(ramp)),
    );
  }

  /// Map (source mode, manual palette, off color) → list of [Color]
  /// stops fed to `renderPaletteRamp`. Empty-list cases collapse to
  /// warm-white inside the renderer, matching the LED ring fallback.
  static List<Color> _stopsFor(
    WispSourceMode mode,
    List<LampColor> manual,
    LampColor offColor,
  ) {
    switch (mode) {
      case WispSourceMode.manual:
        return [for (final c in manual) Color.fromARGB(0xFF, c.r, c.g, c.b)];
      case WispSourceMode.aurora:
        // No app-side Aurora palette today — see file-level comment.
        return const <Color>[];
      case WispSourceMode.off:
        return [Color.fromARGB(0xFF, offColor.r, offColor.g, offColor.b)];
    }
  }
}

/// Paints a pre-computed ramp as `pixelCount` equal-width vertical
/// stripes across the canvas. Cheaper than building a Flutter
/// `LinearGradient` with N stops (which would have to re-interpolate)
/// and pixel-exact with the firmware's integer pipeline.
class _RampPainter extends CustomPainter {
  _RampPainter(this.ramp);

  final List<Color> ramp;

  @override
  void paint(Canvas canvas, Size size) {
    if (ramp.isEmpty || size.width <= 0 || size.height <= 0) return;
    final stripeWidth = size.width / ramp.length;
    final paint = Paint();
    // One stripe per pixel of the precomputed ramp. We pad each rect by
    // a hair so adjacent stripes overlap by ~0.5 px and the canvas
    // doesn't show 1-px gaps from fractional widths on high-DPI screens.
    for (var i = 0; i < ramp.length; i++) {
      paint.color = ramp[i];
      final left = i * stripeWidth;
      // Round up the right edge so the next stripe seam closes.
      final right = (i + 1) * stripeWidth + 0.5;
      canvas.drawRect(Rect.fromLTRB(left, 0, right, size.height), paint);
    }
  }

  @override
  bool shouldRepaint(covariant _RampPainter oldDelegate) {
    if (identical(oldDelegate.ramp, ramp)) return false;
    if (oldDelegate.ramp.length != ramp.length) return true;
    for (var i = 0; i < ramp.length; i++) {
      if (oldDelegate.ramp[i] != ramp[i]) return true;
    }
    return false;
  }
}
