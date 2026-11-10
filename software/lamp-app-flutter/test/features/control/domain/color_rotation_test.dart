import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/color_rotation.dart';

void main() {
  test('period constants are the firmware values', () {
    expect(kRotationMinPeriodS, 5);
    expect(kRotationMaxPeriodS, 120);
    expect(kRotationDefaultPeriodS, 40);
  });

  test('role defaults: shade down, base up', () {
    expect(kShadeDefaultDirection, RotationDirection.down);
    expect(kBaseDefaultDirection, RotationDirection.up);
  });

  test('a never-set role shows the dir its section sends', () {
    // Firmware always sends dir on a role that offers rotation. A never-set
    // shade sends "down" and a never-set base "up".
    final shade = ColorRotation.fromJson(
        {'on': false, 'periodS': 40, 'dir': 'down'},
        defaultDirection: RotationDirection.up);
    final base = ColorRotation.fromJson(
        {'on': false, 'periodS': 40, 'dir': 'up'},
        defaultDirection: RotationDirection.down);
    expect(shade.direction, RotationDirection.down);
    expect(base.direction, RotationDirection.up);
  });

  test('a missing dir takes the default handed in', () {
    for (final d in RotationDirection.values) {
      expect(ColorRotation.fromJson(const {}, defaultDirection: d).direction, d);
    }
  });

  test('mistyped keys take their defaults', () {
    final r = ColorRotation.fromJson(
        {'on': 'yes', 'periodS': '60', 'dir': 7},
        defaultDirection: RotationDirection.down);
    expect(r, const ColorRotation(
        on: false, periodS: 40, direction: RotationDirection.down));
  });

  test('toJson round-trips through fromJson', () {
    const r = ColorRotation(on: true, periodS: 73, direction: RotationDirection.down);
    expect(r.toJson(), {'on': true, 'periodS': 73, 'dir': 'down'});
    expect(ColorRotation.fromJson(r.toJson(), defaultDirection: RotationDirection.up), r);
  });

  group('advanceRotation', () {
    const period = 40;
    // Distance around the loop, so 0.9999999 and 0 read as equal.
    double around(double a, double b) {
      final d = (a - b).abs() % 1.0;
      return d > 0.5 ? 1 - d : d;
    }

    double run(double from, int ticks, Duration dt, int periodS,
        RotationDirection d) {
      var m = from;
      for (var i = 0; i < ticks; i++) {
        m = advanceRotation(m, dt, periodS, d);
      }
      return m;
    }

    const frame = Duration(milliseconds: 16);

    test('advances at one constant rate going up', () {
      final step = advanceRotation(0, frame, period, RotationDirection.up);
      expect(step, closeTo(0.016 / period, 1e-12));
      for (final n in [10, 100, 1000]) {
        expect(run(0, n, frame, period, RotationDirection.up),
            closeTo(n * 0.016 / period, 1e-9));
      }
    });

    test('down runs the other way at the same rate', () {
      for (final start in [0.1, 0.5, 0.9]) {
        final up = advanceRotation(start, frame, period, RotationDirection.up);
        final down = advanceRotation(start, frame, period, RotationDirection.down);
        expect(up - start, closeTo(start - down, 1e-12), reason: '$start');
      }
      expect(advanceRotation(0, frame, period, RotationDirection.down),
          closeTo(1 - 0.016 / period, 1e-12));
    });

    test('stays in [0, 1) and is back at its start after periodS', () {
      for (final d in RotationDirection.values) {
        final end = run(0.25, 2500, frame, period, d);
        expect(end, inInclusiveRange(0, 1));
        expect(end, lessThan(1));
        expect(around(end, 0.25), lessThan(1e-9), reason: d.wire);
        expect(around(advanceRotation(0.25, const Duration(seconds: 3 * period),
                period, d), 0.25), lessThan(1e-9));
      }
    });

    test('the loop length sets the rate', () {
      expect(advanceRotation(0, const Duration(seconds: 5), 10, RotationDirection.up),
          closeTo(0.5, 1e-12));
      expect(advanceRotation(0, const Duration(seconds: 5), 20, RotationDirection.up),
          closeTo(0.25, 1e-12));
    });

    test('steps from where it is, whatever the period was', () {
      // A change of period or direction moves nothing until time passes.
      for (final d in RotationDirection.values) {
        for (final p in [5, 41, 120]) {
          expect(advanceRotation(0.3, Duration.zero, p, d), closeTo(0.3, 1e-12));
        }
      }
    });
  });
}
