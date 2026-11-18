// Regression tests for the TupleSampler algorithm.
// The Dart implementation must stay byte-for-byte in sync with the
// firmware's TupleSampler.cpp or the "Painted lamps" preview silently diverges.

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/domain/tuple_sampler.dart';

void main() {
  // Helper palettes used across the suite.
  const red    = LampColor(r: 255, g: 0, b: 0, w: 0);
  const blue   = LampColor(r: 0, g: 0, b: 255, w: 0);
  const green  = LampColor(r: 0, g: 255, b: 0, w: 0);
  const yellow = LampColor(r: 255, g: 255, b: 0, w: 0);
  const orange = LampColor(r: 255, g: 128, b: 0, w: 0);
  const wispGreen = LampColor(r: 0, g: 200, b: 80, w: 0);

  group('parseMacFromBleId', () {
    test('parses a colon-hex Android MAC', () {
      expect(parseMacFromBleId('AA:BB:CC:01:02:03'),
          equals([0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03]));
    });

    test('returns null for non-MAC ids (iOS opaque UUIDs)', () {
      expect(parseMacFromBleId('B5F1A6D8-1234-5678-ABCD-EF0123456789'),
          isNull);
      expect(parseMacFromBleId(''), isNull);
      expect(parseMacFromBleId('AA:BB:CC'), isNull);
      expect(parseMacFromBleId('not-a-mac'), isNull);
    });
  });

  group('predictTuple', () {
    test('returns null when the palette is empty', () {
      final p = predictTuple(
        mac: const [0x01, 0x02, 0x03, 0x04, 0x05, 0x06],
        palette: const [],
      );
      expect(p, isNull);
    });

    test('returns null when the MAC is malformed', () {
      final p = predictTuple(
        mac: const [0x01, 0x02, 0x03],
        palette: const [red, blue],
      );
      expect(p, isNull);
    });

    test('single-color palette → both surfaces get that color', () {
      const mac = [0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02];
      final p = predictTuple(mac: mac, palette: const [red]);
      expect(p, isNotNull);
      expect(p!.base, equals(red));
      expect(p.shade, equals(red));
    });

    test('two-color palette → gradient sampling produces interpolated colors', () {
      // The new behavior: positions in [0,2^32) map to the continuous gradient,
      // so most MACs should get a blend, not an exact authored endpoint.
      const palette = [orange, wispGreen];
      bool foundBlend = false;
      for (var i = 0; i < 200 && !foundBlend; i++) {
        final mac = [i & 0xFF, (i >> 8) & 0xFF, 0x11, 0x22, 0x33, 0x44];
        final p = predictTuple(mac: mac, palette: palette);
        if (p != null && p.base != orange && p.base != wispGreen) {
          foundBlend = true;
        }
      }
      expect(foundBlend, isTrue,
          reason: 'gradient sampling must produce interpolated colors');
    });

    test('determinism: same (MAC, palette) → same tuple', () {
      const mac = [0x42, 0x42, 0x42, 0x42, 0x42, 0x42];
      const palette = [red, blue, green, yellow];
      final a = predictTuple(mac: mac, palette: palette);
      final b = predictTuple(mac: mac, palette: palette);
      expect(a, isNotNull);
      expect(b, isNotNull);
      expect(a!.base, equals(b!.base));
      expect(a.shade, equals(b.shade));
    });

    test('different shuffleSeed yields different tuple for at least one MAC', () {
      const palette = [red, blue, green, yellow];
      bool foundDiff = false;
      for (var i = 0; i < 256 && !foundDiff; i++) {
        final mac = [i & 0xFF, (i >> 8) & 0xFF, 0xAA, 0xBB, 0xCC, 0xDD];
        final p0 = predictTuple(mac: mac, palette: palette, shuffleSeed: 0);
        final p1 = predictTuple(mac: mac, palette: palette, shuffleSeed: 1);
        if (p0 != null && p1 != null) {
          if (p0.base != p1.base || p0.shade != p1.shade) foundDiff = true;
        }
      }
      expect(foundDiff, isTrue,
          reason: 'shuffleSeed=1 must yield a different result for at least one MAC');
    });

    test('shuffleSeed=0 equals the default (no-seed) call', () {
      const mac = [0x42, 0x42, 0x42, 0x42, 0x42, 0x42];
      const palette = [red, blue, green, yellow];
      final a = predictTuple(mac: mac, palette: palette);
      final b = predictTuple(mac: mac, palette: palette, shuffleSeed: 0);
      expect(a, isNotNull);
      expect(b, isNotNull);
      expect(a!.base, equals(b!.base));
      expect(a.shade, equals(b.shade));
    });

    test('golden parity: mac=10:20:30:40:50:60, orange+green, seed=0', () {
      // Golden values must match the firmware's test_tuple_sampler.cpp byte-for-byte.
      const mac = [0x10, 0x20, 0x30, 0x40, 0x50, 0x60];
      const palette = [orange, wispGreen];
      final p = predictTuple(mac: mac, palette: palette, shuffleSeed: 0);
      expect(p, isNotNull);
      expect(p!.base,  equals(const LampColor(r: 178, g: 149, b: 23, w: 0)));
      expect(p.shade, equals(const LampColor(r: 114, g: 167, b: 43, w: 0)));
    });

    test('dedupe collapses near-identical adjacent stops before picking', () {
      // Three "colors" but the first two are within the 8/255 delta —
      // dedupe drops the second, leaving n=2 effective colors.
      const palette = [
        LampColor(r: 100, g: 100, b: 100, w: 0),
        LampColor(r: 103, g: 102, b: 101, w: 0),
        LampColor(r: 0, g: 0, b: 0, w: 0),
      ];
      // Just verify it doesn't crash and returns non-null.
      const mac = [0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56];
      final p = predictTuple(mac: mac, palette: palette);
      expect(p, isNotNull);
    });
  });

  // Swatch vectors, case for case with the swatch tests in the wisp's
  // test/test_tuple_sampler/tuple_sampler.cpp; change both together.
  group('swatches', () {
    const four = [
      LampColor(r: 255, g: 0, b: 0, w: 0),
      LampColor(r: 0, g: 255, b: 0, w: 0),
      LampColor(r: 0, g: 0, b: 255, w: 0),
      LampColor(r: 0, g: 0, b: 0, w: 255),
    ];

    TuplePrediction at(List<LampColor> palette, int posA, int posB) =>
        predictTupleAtPositions(
          palette: palette,
          posA: posA,
          posB: posB,
          style: PaletteStyle.swatches,
        )!;

    TuplePrediction forMac(List<LampColor> palette, List<int> mac, int seed) =>
        predictTuple(
          mac: mac,
          palette: palette,
          shuffleSeed: seed,
          style: PaletteStyle.swatches,
        )!;

    test('positions pick entries (test_swatch_positions_pick_entries)', () {
      const cases = [
        (0x00000000, 0x00000000, 0, 1),
        (0xFFFFFFFF, 0xFFFFFFFF, 3, 2),
        (0x80000000, 0x80000000, 2, 1),
        (0x40000000, 0xC0000000, 1, 3),
      ];
      for (final (posA, posB, base, shade) in cases) {
        final t = at(four, posA, posB);
        expect(four.indexOf(t.base), base);
        expect(four.indexOf(t.shade), shade);
      }
      const a = LampColor(r: 10, g: 20, b: 30, w: 40);
      const b = LampColor(r: 50, g: 60, b: 70, w: 80);
      const c = LampColor(r: 90, g: 100, b: 110, w: 120);
      final t = at(const [a, b, a, c], 0, 0x80000000);
      expect(t.base, a);
      expect(t.shade, c);
    });

    test('golden per mac, seeds 0 and 7 (test_swatch_golden_per_mac)', () {
      const want = [
        [[2, 3], [0, 1], [3, 1], [0, 2], [1, 0], [1, 2]], // seed 0
        [[0, 1], [1, 3], [3, 0], [1, 0], [0, 1], [2, 3]], // seed 7
      ];
      const seeds = [0, 7];
      for (var s = 0; s < 2; s++) {
        for (var i = 0; i < 6; i++) {
          final t = forMac(four, [0x10, 0x20, 0x30, 0x40, 0x50, i], seeds[s]);
          expect([four.indexOf(t.base), four.indexOf(t.shade)], want[s][i],
              reason: 'seed ${seeds[s]} mac $i');
        }
      }
    });

    test('base weight follows repetition '
        '(test_swatch_base_weight_follows_repetition)', () {
      const a = LampColor(r: 200, g: 40, b: 10, w: 0);
      const b = LampColor(r: 10, g: 40, b: 200, w: 0);
      var baseIsA = 0;
      for (var i = 0; i < 64; i++) {
        final t = forMac(
            const [a, a, a, b], [0x24, 0x6f, 0x28, 0x00, i >> 4, i & 15], 0);
        expect(t.base == a || t.base == b, isTrue);
        expect(t.base, isNot(t.shade));
        if (t.base == a) baseIsA++;
      }
      // 48 expected; a palette weighed as [A, B] lands near 32.
      expect(baseIsA, inInclusiveRange(40, 56));
    });

    test('a single colour fills both (test_swatch_single_colour_fills_both)',
        () {
      const one = LampColor(r: 12, g: 34, b: 56, w: 78);
      final t = forMac(const [one], [0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02], 3);
      expect(t.base, one);
      expect(t.shade, one);
    });

    test('shuffle changes picks (test_swatch_shuffle_changes_picks)', () {
      var changed = false;
      for (var i = 0; i < 6 && !changed; i++) {
        final mac = [0x10, 0x20, 0x30, 0x40, 0x50, i];
        final t0 = forMac(four, mac, 0);
        final t7 = forMac(four, mac, 7);
        changed = t0.base != t7.base || t0.shade != t7.shade;
      }
      expect(changed, isTrue);
    });

    test('near-duplicates stay separate (test_swatch_keeps_near_duplicates)',
        () {
      const near = [
        LampColor(r: 100, g: 0, b: 0, w: 0),
        LampColor(r: 104, g: 0, b: 0, w: 0),
      ];
      final lo = at(near, 0, 0);
      final hi = at(near, 0xFFFFFFFF, 0);
      expect([near.indexOf(lo.base), near.indexOf(lo.shade)], [0, 1]);
      expect([near.indexOf(hi.base), near.indexOf(hi.shade)], [1, 0]);
    });

    test('white-only difference counts '
        '(test_swatch_white_only_difference_counts)', () {
      const pair = [
        LampColor(r: 200, g: 100, b: 0, w: 0),
        LampColor(r: 200, g: 100, b: 0, w: 255),
      ];
      final t = at(pair, 0, 0);
      expect([pair.indexOf(t.base), pair.indexOf(t.shade)], [0, 1]);
    });

    test('random draws are two different entries '
        '(test_drift_draws_use_the_style)', () {
      var x = 0x12345678;
      int next() => x = (x * 1664525 + 1013904223) & 0xFFFFFFFF;
      for (var i = 0; i < 200; i++) {
        final posA = next();
        final posB = next();
        final t = at(four, posA, posB);
        expect(four, contains(t.base));
        expect(four, contains(t.shade));
        expect(t.base, isNot(t.shade));
      }
    });

    test('gradient is the default and unchanged by the style parameter', () {
      const mac = [0x10, 0x20, 0x30, 0x40, 0x50, 0x60];
      const palette = [orange, wispGreen];
      final p = predictTuple(
          mac: mac, palette: palette, style: PaletteStyle.gradient)!;
      expect(p.base, const LampColor(r: 178, g: 149, b: 23, w: 0));
      expect(p.shade, const LampColor(r: 114, g: 167, b: 43, w: 0));
    });
  });
}
