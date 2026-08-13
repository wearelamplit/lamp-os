import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/wisp/domain/wisp_claims.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';

void main() {
  group('parseClaimedMacs', () {
    test('empty blob → empty set', () {
      expect(parseClaimedMacs([]), isEmpty);
    });

    test('count=0 → empty set', () {
      expect(parseClaimedMacs([0x00]), isEmpty);
    });

    test('two MACs parse to correct uppercase colon-hex strings', () {
      final blob = [
        0x02,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
      ];
      final result = parseClaimedMacs(blob);
      expect(result, {'AA:BB:CC:DD:EE:FF', '01:02:03:04:05:06'});
    });

    test('truncated blob (count says 2 but only 1 MAC present) → empty set', () {
      final blob = [
        0x02,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, // missing last byte of first MAC
      ];
      expect(parseClaimedMacs(blob), isEmpty);
    });

    test('count larger than data length → empty set (no throw)', () {
      final blob = [0x20]; // count=32 but no MAC bytes at all
      expect(parseClaimedMacs(blob), isEmpty);
    });

    test('single MAC round-trips', () {
      final blob = [0x01, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60];
      final result = parseClaimedMacs(blob);
      expect(result, {'10:20:30:40:50:60'});
    });

    test('low nibble bytes format correctly (pad with leading zero)', () {
      final blob = [0x01, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F];
      final result = parseClaimedMacs(blob);
      expect(result, {'0A:0B:0C:0D:0E:0F'});
    });

    test('blob with colors: parseClaimedMacs still returns only the macs', () {
      // count=1, mac, baseRGB, shadeRGB appended
      final blob = [
        0x01,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0xFF, 0x00, 0x80, // base RGB
        0x00, 0xFF, 0x40, // shade RGB
      ];
      expect(parseClaimedMacs(blob), {'AA:BB:CC:DD:EE:FF'});
    });
  });

  group('parseWispClaims', () {
    test('empty blob → empty macs + empty colors', () {
      final r = parseWispClaims([]);
      expect(r.macs, isEmpty);
      expect(r.colors, isEmpty);
    });

    test('count=0 → empty macs + empty colors', () {
      final r = parseWispClaims([0x00]);
      expect(r.macs, isEmpty);
      expect(r.colors, isEmpty);
    });

    test('malformed/short blob → empty (no throw)', () {
      final r = parseWispClaims([0x05, 0xAA, 0xBB]);
      expect(r.macs, isEmpty);
      expect(r.colors, isEmpty);
    });

    test('legacy mac-only blob → macs parsed, empty color map', () {
      final blob = [
        0x02,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
      ];
      final r = parseWispClaims(blob);
      expect(r.macs, {'AA:BB:CC:DD:EE:FF', '01:02:03:04:05:06'});
      expect(r.colors, isEmpty);
    });

    test('blob with colors: returns macs AND color map with exact RGB', () {
      final blob = [
        0x02,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // mac[0]
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // mac[1]
        0xFF, 0x80, 0x00, 0x00, 0x40, 0xFF, // colorPair[0]: base+shade
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, // colorPair[1]: base+shade
      ];
      final r = parseWispClaims(blob);
      expect(r.macs, {'AA:BB:CC:DD:EE:FF', '01:02:03:04:05:06'});
      expect(r.colors.length, 2);

      final c0 = r.colors['AA:BB:CC:DD:EE:FF']!;
      expect(c0.base, const LampColor(r: 0xFF, g: 0x80, b: 0x00, w: 0));
      expect(c0.shade, const LampColor(r: 0x00, g: 0x40, b: 0xFF, w: 0));

      final c1 = r.colors['01:02:03:04:05:06']!;
      expect(c1.base, const LampColor(r: 0x10, g: 0x20, b: 0x30, w: 0));
      expect(c1.shade, const LampColor(r: 0x40, g: 0x50, b: 0x60, w: 0));
    });

    test('sentinel pair (all-zero) → mac in set but omitted from color map', () {
      final blob = [
        0x02,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // mac[0]
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // mac[1]
        0xFF, 0x80, 0x00, 0x00, 0x40, 0xFF, // colorPair[0]: real color
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // colorPair[1]: sentinel
      ];
      final r = parseWispClaims(blob);
      expect(r.macs, {'AA:BB:CC:DD:EE:FF', '01:02:03:04:05:06'});
      // Sentinel mac omitted from color map
      expect(r.colors.containsKey('01:02:03:04:05:06'), isFalse);
      // Non-sentinel mac present
      expect(r.colors.containsKey('AA:BB:CC:DD:EE:FF'), isTrue);
    });

    test('color section truncated → treated as legacy (no colors)', () {
      // count=2, 2 macs, only 1 color pair (should be 2) — partial color section
      final blob = [
        0x02,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0xFF, 0x80, 0x00, 0x00, 0x40, 0xFF, // only one pair, not two
      ];
      final r = parseWispClaims(blob);
      expect(r.macs, {'AA:BB:CC:DD:EE:FF', '01:02:03:04:05:06'});
      expect(r.colors, isEmpty);
    });
  });
}
