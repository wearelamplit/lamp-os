import 'dart:math' as math;

import 'package:collection/collection.dart';

import 'expression_catalog.dart';

/// Dart port of the firmware `applyEasing`
/// (`software/lamp-os/src/util/easing.hpp`). Keep in lockstep so the Motion
/// picker's sparklines match on-lamp motion. Values mirror the firmware
/// `Easing` enum: 0 Linear, 1 Smooth, 2 Float, 3 Settle, 4 Swell, 5-7 Snap
/// (In/Out/In-out), 8-10 Overshoot, 11-13 Spring, 14-16 Bounce, 17 Random
/// (no curve — resolved on-lamp per fire, never passed here).

const double _floatDwell = 0.12;

/// Firmware `Easing::Random`. No curve — the lamp resolves it to a concrete
/// value per fire; callers must not draw a sparkline for it.
const int kRandomEasingValue = 17;

double _outExpo(double t) => t >= 1.0 ? 1.0 : 1.0 - math.pow(2.0, -10.0 * t);

double _outBack(double t) {
  const c1 = 1.70158;
  const c3 = c1 + 1.0;
  final u = t - 1.0;
  return 1.0 + c3 * u * u * u + c1 * u * u;
}

double _outElastic(double t) {
  if (t <= 0.0 || t >= 1.0) return t;
  const c4 = 2.0 * math.pi / 3.0;
  return math.pow(2.0, -10.0 * t) * math.sin((t * 10.0 - 0.75) * c4) + 1.0;
}

double _outBounce(double t) {
  const n1 = 7.5625;
  const d1 = 2.75;
  if (t < 1.0 / d1) return n1 * t * t;
  if (t < 2.0 / d1) {
    final u = t - 1.5 / d1;
    return n1 * u * u + 0.75;
  }
  if (t < 2.5 / d1) {
    final u = t - 2.25 / d1;
    return n1 * u * u + 0.9375;
  }
  final u = t - 2.625 / d1;
  return n1 * u * u + 0.984375;
}

double _easeIn(double Function(double) fOut, double t) => 1.0 - fOut(1.0 - t);

double _easeInOut(double Function(double) fOut, double t) => t < 0.5
    ? (1.0 - fOut(1.0 - 2.0 * t)) * 0.5
    : (1.0 + fOut(2.0 * t - 1.0)) * 0.5;

/// Maps progress [t] in [0,1] through easing curve [value]; clamps to [0,1].
/// [value] 17 (Random) has no curve — callers must resolve it to a concrete
/// value before calling this.
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
    case 5:
      return _easeIn(_outExpo, t);
    case 6:
      return _outExpo(t);
    case 7:
      return _easeInOut(_outExpo, t);
    case 8:
      return _easeIn(_outBack, t);
    case 9:
      return _outBack(t);
    case 10:
      return _easeInOut(_outBack, t);
    case 11:
      return _easeIn(_outElastic, t);
    case 12:
      return _outElastic(t);
    case 13:
      return _easeInOut(_outElastic, t);
    case 14:
      return _easeIn(_outBounce, t);
    case 15:
      return _outBounce(t);
    case 16:
      return _easeInOut(_outBounce, t);
    default:
      return t;
  }
}

/// One `easing` catalog family (a `group`) with its ordered Direction cells.
/// A singleton motion (Linear, Float, Random) has exactly one direction,
/// labeled by its own option's `label`.
class EasingFamily {
  const EasingFamily({required this.family, required this.directions});

  final String family;
  final List<({String label, int value})> directions;
}

/// Decomposes a flat `easing` option list into families, preserving
/// first-seen catalog order. An option with no `group` (legacy catalog)
/// becomes its own singleton family named by its `label`.
List<EasingFamily> groupEasing(List<EnumOption> options) {
  final order = <String>[];
  final directions = <String, List<({String label, int value})>>{};
  for (final o in options) {
    final family = o.group ?? o.label;
    if (!directions.containsKey(family)) {
      order.add(family);
      directions[family] = [];
    }
    directions[family]!.add((label: o.label, value: o.value));
  }
  return [for (final f in order) EasingFamily(family: f, directions: directions[f]!)];
}

/// The catalog value for [family]/[direction]; a singleton family resolves
/// to its sole value regardless of [direction]. Null if [family] isn't in
/// [options].
int? resolveEasing(List<EnumOption> options, String family, String direction) {
  final f = groupEasing(options).firstWhereOrNull((f) => f.family == family);
  if (f == null) return null;
  return (f.directions.firstWhereOrNull((d) => d.label == direction) ??
          f.directions.first)
      .value;
}

/// Reverse lookup: which family/direction [value] currently selects.
({String family, String direction}) describeEasing(
    List<EnumOption> options, int value) {
  final o = options.firstWhereOrNull((o) => o.value == value) ?? options.first;
  return (family: o.group ?? o.label, direction: o.label);
}

/// One-liner shown under each family in the Motion picker, keyed by the
/// catalog enum option's `group`.
const Map<String, String> easingBlurbs = {
  'Linear': 'Steady as she goes — same pace, start to finish.',
  'Smooth': 'Eases in and out. One slow, easy breath.',
  'Float': 'Drifts up, hangs at the top, sinks back. Pure lava-lamp lazy.',
  'Snap': 'Whips to full fast, then coasts. Sharp and punchy.',
  'Overshoot': 'Blows past the mark, then eases back to land.',
  'Spring': 'Springs in and wobbles before it settles.',
  'Bounce': 'Hits and hops a few times like a dropped ball.',
  'Random': 'Rolls a new motion every time it fires.',
};
