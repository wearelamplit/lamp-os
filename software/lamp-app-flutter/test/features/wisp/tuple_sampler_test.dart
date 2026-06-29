// Parity / regression tests for the Dart port of the wisp's
// `sampleTupleForMac` algorithm.
//
// The C++ implementation lives at `software/wisp/src/TupleSampler.cpp`
// and the Dart port mirrors it at `lib/features/wisp/domain/tuple_sampler.dart`.
// If either drifts, the wisp config screen's "Painted lamps" preview
// will diverge from what the wisp actually paints — a silent UX bug.
// These tests pin the contract.

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/domain/tuple_sampler.dart';

void main() {
  // Helper palettes used across the suite.
  const red    = LampColor(r: 255, g: 0, b: 0, w: 0);
  const blue   = LampColor(r: 0, g: 0, b: 255, w: 0);
  const green  = LampColor(r: 0, g: 255, b: 0, w: 0);
  const yellow = LampColor(r: 255, g: 255, b: 0, w: 0);

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

  group('meshMacFromBdAddr', () {
    test('subtracts 2 (ESP32 BLE -> STA/mesh)', () {
      expect(meshMacFromBdAddr('FC:B4:67:F1:DD:A6'),
          equals([0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA4]));
    });

    test('borrows across the octet boundary', () {
      expect(meshMacFromBdAddr('AA:BB:CC:DD:EE:01'),
          equals([0xAA, 0xBB, 0xCC, 0xDD, 0xED, 0xFF]));
    });

    test('null for non-MAC ids', () {
      expect(meshMacFromBdAddr('opaque-uuid'), isNull);
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

    test('two-color palette → each surface is one of the authored colors '
        '(byte-exact, never a blend)', () {
      const mac = [0x10, 0x20, 0x30, 0x40, 0x50, 0x60];
      final p = predictTuple(mac: mac, palette: const [red, blue]);
      expect(p, isNotNull);
      expect([red, blue], contains(p!.base));
      expect([red, blue], contains(p.shade));
    });

    test('multi-color palette → base and shade are distinct authored colors '
        '(the idxA==idxB collision bump kicks in when n >= 2)', () {
      const palette = [red, blue, green, yellow];
      for (var i = 0; i < 100; i++) {
        final mac = [i & 0xFF, (i >> 8) & 0xFF, 0xAA, 0xBB, 0xCC, 0xDD];
        final p = predictTuple(mac: mac, palette: palette);
        expect(p, isNotNull);
        expect(p!.base, isNot(equals(p.shade)),
            reason: 'base and shade must differ for MAC=$mac');
      }
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

    test('swap distribution: across a wide MAC sweep on a 2-color palette, '
        'base lands ~50/50 on each authored color', () {
      // The headline behavioural fix. Without the per-MAC swap bit, the
      // fleet visibly clamped to "all blue tops, red bottoms"; with it,
      // both base assignments should be within 30-70%.
      const palette = [red, blue];
      var baseRed = 0;
      var baseBlue = 0;
      const sweep = 1024;
      for (var i = 0; i < sweep; i++) {
        final mac = [i & 0xFF, (i >> 8) & 0xFF, (i >> 16) & 0xFF,
                     0x99, 0x77, 0x55];
        final p = predictTuple(mac: mac, palette: palette);
        if (p!.base == red) baseRed++;
        if (p.base == blue) baseBlue++;
      }
      expect(baseRed + baseBlue, equals(sweep),
          reason: 'every MAC must resolve to one authored stop');
      const lower = (sweep * 30) ~/ 100;
      const upper = (sweep * 70) ~/ 100;
      expect(baseRed, inInclusiveRange(lower, upper),
          reason: 'swap bit must keep base=red within 30-70%');
      expect(baseBlue, inInclusiveRange(lower, upper),
          reason: 'swap bit must keep base=blue within 30-70%');
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

    test('dedupe collapses near-identical adjacent stops before picking', () {
      // Three "colors" but the first two are within the 8/255 delta —
      // dedupe drops the second, leaving n=2 effective colors.
      const palette = [
        LampColor(r: 100, g: 100, b: 100, w: 0),
        LampColor(r: 103, g: 102, b: 101, w: 0),
        LampColor(r: 0, g: 0, b: 0, w: 0),
      ];
      const mac = [0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56];
      final p = predictTuple(mac: mac, palette: palette);
      expect(p, isNotNull);
      // After dedupe the effective palette is just two distinct colors;
      // both surfaces must land on one of those two (never the near-duplicate
      // intermediate stop).
      const allowed = [
        LampColor(r: 100, g: 100, b: 100, w: 0),
        LampColor(r: 0, g: 0, b: 0, w: 0),
      ];
      expect(allowed, contains(p!.base));
      expect(allowed, contains(p.shade));
    });
  });
}
