import 'dart:math' as math;

/// Pure math for converting between raw interval seconds and log-scale slider
/// positions on the Expression editor's Frequency/Predictability controls.
///
/// The slider track is log-scaled so the common 10–60 s band occupies
/// proportional space instead of <2% of a linear 10–3600 s track.
abstract class ExpressionIntervalMath {
  static const int minSec = 10;
  static const int maxSec = 3600;
  static const double maxMultiplier = 5.0;

  /// Log10 position for [sec].
  static double secToPos(int sec) => math.log(sec) / math.ln10;

  /// Nearest second for log10 slider position [pos], clamped to [minSec, maxSec].
  static int posToSec(double pos) =>
      math.pow(10, pos).round().clamp(minSec, maxSec);
}
