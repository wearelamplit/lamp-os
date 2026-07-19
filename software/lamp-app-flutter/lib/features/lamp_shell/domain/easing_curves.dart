import 'dart:math' as math;

/// Dart port of the firmware `applyEasing`
/// (`software/lamp-os/src/util/easing.hpp`). Keep in lockstep so the Motion
/// picker's sparklines match on-lamp motion. Values mirror the firmware
/// `Easing` enum: 0 Linear, 1 Smooth, 2 Float, 3 Settle, 4 Swell.

const double _floatDwell = 0.12;

/// Maps progress [t] in [0,1] through easing curve [value]; clamps to [0,1].
double applyEasing(int value, double t) {
  if (t <= 0.0) return 0.0;
  if (t >= 1.0) return 1.0;
  switch (value) {
    case 1:
      return t * t * (3.0 - 2.0 * t);
    case 2:
      if (t <= _floatDwell) return 0.0;
      if (t >= 1.0 - _floatDwell) return 1.0;
      final u = (t - _floatDwell) / (1.0 - 2.0 * _floatDwell);
      return 0.5 - 0.5 * math.cos(math.pi * u);
    case 3:
      return 1.0 - (1.0 - t) * (1.0 - t);
    case 4:
      return t * t;
    default:
      return t;
  }
}

/// One-liner shown under each option in the Motion picker, keyed by the
/// catalog enum option's label.
const Map<String, String> easingBlurbs = {
  'Linear': 'Steady as she goes — same pace, start to finish.',
  'Smooth': 'Eases in, eases out. One slow, easy breath.',
  'Float': 'Drifts up, hangs at the top, sinks back down. Pure lava-lamp lazy.',
  'Settle': 'Rushes in, then relaxes into place — like an ember cooling.',
  'Swell': 'Bides its time, then blooms all at once.',
};
