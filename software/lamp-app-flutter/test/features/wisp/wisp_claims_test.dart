import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/wisp/domain/wisp_claims.dart';

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
  });

  group('macBytesToString', () {
    test('matches parseClaimedMacs output format', () {
      // parseMacFromBleId('AA:BB:CC:DD:EE:FF') would give these bytes;
      // macBytesToString must produce the same string for the filter to work.
      final mac = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];
      expect(macBytesToString(mac), 'AA:BB:CC:DD:EE:FF');
    });

    test('low-nibble bytes get leading zero', () {
      final mac = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06];
      expect(macBytesToString(mac), '01:02:03:04:05:06');
    });
  });
}
