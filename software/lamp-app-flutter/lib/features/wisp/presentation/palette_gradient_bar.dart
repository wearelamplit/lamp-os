// Full-width palette gradient bar at the top of the wisp pane.
// Aurora mode falls back to warm-white: the wisp publishes only a
// paletteId prefix over BLE, not the full stops (carrying them would
// exceed CONTROL_MAX_PAYLOAD). See domain/palette_gradient.dart for math.

import 'package:flutter/material.dart';

import '../../control/domain/lamp_color.dart';
import '../domain/palette_gradient.dart';
import '../domain/wisp_source_mode.dart';

/// Horizontal bar at the very top of the wisp pane. Stretches edge-to-
/// edge (no padding), 36 px tall by default: enough that the gradient
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

  /// Manual palette to render when [sourceMode] is [WispSourceMode.manual].
  /// Pass the editor draft so the bar updates live on every swatch edit.
  final List<LampColor> manualPalette;

  /// Operator-chosen color for the wisp ring when [sourceMode] is
  /// [WispSourceMode.off]. Rendered as a single solid stop.
  final LampColor offColor;

  /// Vertical extent of the bar in logical pixels.
  final double height;

  /// Number of sample points across the bar.
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
        // No app-side Aurora palette; see file-level comment.
        return const <Color>[];
      case WispSourceMode.off:
        return [Color.fromARGB(0xFF, offColor.r, offColor.g, offColor.b)];
    }
  }
}

/// Paints a pre-computed ramp as equal-width vertical stripes.
/// Pixel-exact with the firmware integer pipeline at the same sample count.
class _RampPainter extends CustomPainter {
  _RampPainter(this.ramp);

  final List<Color> ramp;

  @override
  void paint(Canvas canvas, Size size) {
    if (ramp.isEmpty || size.width <= 0 || size.height <= 0) return;
    final stripeWidth = size.width / ramp.length;
    final paint = Paint();
    // Pad each rect by 0.5 px so adjacent stripes overlap and fractional
    // widths don't leave 1-px gaps on high-DPI screens.
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
