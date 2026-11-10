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
    // Firmware always sends dir on a role that offers rotation; a never-set
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

  group('rotationPosition', () {
    const period = 40;
    Duration at(double loops) =>
        Duration(microseconds: (loops * period * 1e6).round());

    test('advances at one constant rate going up', () {
      final samples = [for (var i = 0; i < 8; i++) rotationPosition(at(i / 8), period, RotationDirection.up)];
      for (var i = 0; i < 8; i++) {
        expect(samples[i], closeTo(i / 8, 1e-9));
      }
    });

    test('down runs the other way at the same rate', () {
      for (final loops in [0.1, 0.37, 0.5, 0.9]) {
        final up = rotationPosition(at(loops), period, RotationDirection.up);
        final down = rotationPosition(at(loops), period, RotationDirection.down);
        expect((up + down) % 1.0, closeTo(0, 1e-9), reason: '$loops');
        expect(down, closeTo(1 - loops, 1e-9));
      }
    });

    test('is back at its start after periodS, either way', () {
      for (final d in RotationDirection.values) {
        expect(rotationPosition(Duration.zero, period, d), 0);
        expect(rotationPosition(const Duration(seconds: period), period, d), 0);
        expect(rotationPosition(const Duration(seconds: 3 * period), period, d), 0);
        expect(rotationPosition(at(1.25), period, d),
            closeTo(rotationPosition(at(0.25), period, d), 1e-9));
      }
    });

    test('the loop length sets the rate', () {
      expect(rotationPosition(const Duration(seconds: 5), 10, RotationDirection.up),
          closeTo(0.5, 1e-9));
      expect(rotationPosition(const Duration(seconds: 5), 20, RotationDirection.up),
          closeTo(0.25, 1e-9));
    });
  });
}
