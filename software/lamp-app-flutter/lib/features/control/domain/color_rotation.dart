/// Which way the colours travel along a surface: physically up or down on the
/// lamp. Wire form "up" / "down".
enum RotationDirection {
  up('up'),
  down('down');

  const RotationDirection(this.wire);
  final String wire;

  static RotationDirection? fromWire(Object? v) {
    for (final d in values) {
      if (d.wire == v) return d;
    }
    return null;
  }
}

/// Loop length bounds and default, in whole seconds. Must match the firmware's
/// `kRotationMinPeriodS` / `kRotationMaxPeriodS` / `kRotationDefaultPeriodS`.
const int kRotationMinPeriodS = 5;
const int kRotationMaxPeriodS = 120;
const int kRotationDefaultPeriodS = 40;

/// Each role's direction when it never set one. Must match the firmware's
/// per-role defaults.
const RotationDirection kShadeDefaultDirection = RotationDirection.down;
const RotationDirection kBaseDefaultDirection = RotationDirection.up;

/// One role's colour rotation: the `rotate` object of the base and shade page
/// sections and of the colours-write object form.
class ColorRotation {
  const ColorRotation({
    required this.on,
    required this.periodS,
    required this.direction,
  });

  final bool on;

  /// Seconds for one full loop through the colours, [kRotationMinPeriodS] to
  /// [kRotationMaxPeriodS].
  final int periodS;
  final RotationDirection direction;

  /// Reads a `rotate` object. A missing or mistyped key takes its default;
  /// [defaultDirection] is the role's.
  factory ColorRotation.fromJson(
    Map<String, dynamic> json, {
    required RotationDirection defaultDirection,
  }) {
    final period = json['periodS'];
    return ColorRotation(
      on: json['on'] is bool ? json['on'] as bool : false,
      periodS: period is int
          ? period.clamp(kRotationMinPeriodS, kRotationMaxPeriodS)
          : kRotationDefaultPeriodS,
      direction: RotationDirection.fromWire(json['dir']) ?? defaultDirection,
    );
  }

  Map<String, dynamic> toJson() =>
      {'on': on, 'periodS': periodS, 'dir': direction.wire};

  ColorRotation copyWith({
    bool? on,
    int? periodS,
    RotationDirection? direction,
  }) =>
      ColorRotation(
        on: on ?? this.on,
        periodS: periodS ?? this.periodS,
        direction: direction ?? this.direction,
      );

  @override
  bool operator ==(Object other) =>
      other is ColorRotation &&
      on == other.on &&
      periodS == other.periodS &&
      direction == other.direction;

  @override
  int get hashCode => Object.hash(on, periodS, direction);

  @override
  String toString() =>
      'ColorRotation(on: $on, periodS: $periodS, ${direction.wire})';
}

/// How far a rotating preview's pattern has moved toward the top of the
/// preview after [elapsed], as a fraction of one loop in [0, 1). One constant
/// rate, one loop per [periodS]; [RotationDirection.down] runs the other way.
double rotationPosition(
  Duration elapsed,
  int periodS,
  RotationDirection direction,
) {
  final periodUs = periodS * Duration.microsecondsPerSecond;
  final f = (elapsed.inMicroseconds % periodUs) / periodUs;
  if (direction == RotationDirection.up) return f;
  return f == 0 ? 0 : 1 - f;
}
