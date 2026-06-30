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
      // Must match the C++ golden in test_tuple_sampler/tuple_sampler.cpp byte-for-byte.
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
}
