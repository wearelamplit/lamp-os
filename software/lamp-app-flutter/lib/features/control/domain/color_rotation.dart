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

  /// Reads a `rotate` object. A missing or mistyped key takes its default,
  /// and [defaultDirection] is the role's.
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

/// Advances a rotating preview's position by [dt]. [moved] is how far its
/// pattern has moved toward the top of the preview, as a fraction of one loop
/// in [0, 1). One constant rate, one loop per [periodS], and
/// [RotationDirection.down] runs the other way. Stepping from the current
/// position keeps the frame where it is when the period or direction changes,
/// as the lamp does.
double advanceRotation(
  double moved,
  Duration dt,
  int periodS,
  RotationDirection direction,
) {
  final step = dt.inMicroseconds / (periodS * Duration.microsecondsPerSecond);
  final next = direction == RotationDirection.up ? moved + step : moved - step;
  return next - next.floorToDouble();
}
