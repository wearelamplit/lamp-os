import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/lamp_shell/domain/expression_interval_math.dart';

void main() {
  group('ExpressionIntervalMath.intervalFromNorms', () {
    test('freq=0 spread=0 → (3600,3600)', () {
      final r = ExpressionIntervalMath.intervalFromNorms(0, 0);
      expect(r.min, 3600); expect(r.max, 3600);
    });
    test('freq=1 spread=0 → (10,10)', () {
      final r = ExpressionIntervalMath.intervalFromNorms(1, 0);
      expect(r.min, 10); expect(r.max, 10);
    });
    test('freq=0.5 spread=0 → ~190 (geometric mid)', () {
      final r = ExpressionIntervalMath.intervalFromNorms(0.5, 0);
      expect(r.min, inInclusiveRange(185, 195));
      expect(r.min, r.max);
    });
    test('freq=0.5 spread=1 → wide range around 190', () {
      final r = ExpressionIntervalMath.intervalFromNorms(0.5, 1);
      expect(r.max, greaterThan(r.min * 4));
      expect(r.min, greaterThanOrEqualTo(10));
      expect(r.max, lessThanOrEqualTo(3600));
    });
    test('clamping does not throw at extremes', () {
      expect(() => ExpressionIntervalMath.intervalFromNorms(0, 1), returnsNormally);
      expect(() => ExpressionIntervalMath.intervalFromNorms(1, 1), returnsNormally);
    });
  });

  group('ExpressionIntervalMath.normsFromInterval', () {
    test('(10,3600) → freq≈0.5 spread≈1.0', () {
      final n = ExpressionIntervalMath.normsFromInterval(10, 3600);
      expect(n.freq, closeTo(0.5, 0.02));
      expect(n.spread, closeTo(1.0, 0.02));
    });
    test('tolerates min>max', () {
      final a = ExpressionIntervalMath.normsFromInterval(900, 60);
      final b = ExpressionIntervalMath.normsFromInterval(60, 900);
      expect(a.freq, closeTo(b.freq, 1e-6));
      expect(a.spread, closeTo(b.spread, 1e-6));
    });
    test('round-trip interior values within ±0.02', () {
      for (final f in [0.25, 0.5, 0.75]) {
        for (final s in [0.25, 0.5, 0.75]) {
          final r = ExpressionIntervalMath.intervalFromNorms(f, s);
          final back = ExpressionIntervalMath.normsFromInterval(r.min, r.max);
          expect(back.freq, closeTo(f, 0.02));
          expect(back.spread, closeTo(s, 0.02));
        }
      }
    });
  });
}
