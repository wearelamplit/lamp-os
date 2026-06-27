import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/lamp_shell/domain/expression_interval_math.dart';

void main() {
  group('ExpressionIntervalMath constants', () {
    test('minSec = 10', () => expect(ExpressionIntervalMath.minSec, 10));
    test('maxSec = 3600', () => expect(ExpressionIntervalMath.maxSec, 3600));
  });

  group('ExpressionIntervalMath.secToPos', () {
    test('10 s → log10(10) = 1.0', () {
      expect(ExpressionIntervalMath.secToPos(10), closeTo(1.0, 1e-9));
    });
    test('60 s → log10(60) ≈ 1.778', () {
      expect(ExpressionIntervalMath.secToPos(60), closeTo(math.log(60) / math.ln10, 1e-9));
    });
    test('3600 s → log10(3600) ≈ 3.556', () {
      expect(ExpressionIntervalMath.secToPos(3600), closeTo(math.log(3600) / math.ln10, 1e-9));
    });
    test('60 s pos sits above linear midpoint of [1.0, 3.556]', () {
      // log10(60) ≈ 1.778; linear midpoint ≈ 2.278 — 60 s is in the lower half
      // of the log range, confirming the log scale compresses the high end.
      final pos60 = ExpressionIntervalMath.secToPos(60);
      final posMin = ExpressionIntervalMath.secToPos(ExpressionIntervalMath.minSec);
      final posMax = ExpressionIntervalMath.secToPos(ExpressionIntervalMath.maxSec);
      expect(pos60, greaterThan(posMin));
      expect(pos60, lessThan((posMin + posMax) / 2));
    });
  });

  group('ExpressionIntervalMath.posToSec', () {
    test('clamps below minSec', () {
      expect(ExpressionIntervalMath.posToSec(0.0), ExpressionIntervalMath.minSec);
    });
    test('clamps above maxSec', () {
      expect(ExpressionIntervalMath.posToSec(5.0), ExpressionIntervalMath.maxSec);
    });
  });

  group('secToPos/posToSec round-trip', () {
    void roundTrip(int sec) {
      final pos = ExpressionIntervalMath.secToPos(sec);
      final back = ExpressionIntervalMath.posToSec(pos);
      expect(back, sec,
          reason: 'round-trip failed for ${sec}s: pos=$pos → $back');
    }

    test('10 s round-trips exactly', () => roundTrip(10));
    test('60 s round-trips exactly', () => roundTrip(60));
    test('3600 s round-trips exactly', () => roundTrip(3600));
  });
}
