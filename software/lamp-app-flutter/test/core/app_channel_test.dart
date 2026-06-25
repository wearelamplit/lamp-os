import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/app_channel.dart';

void main() {
  group('formatFirmwareSemver', () {
    test('decodes packed semver into M.m.p string', () {
      // FIRMWARE_VERSION layout: (major << 16) | (minor << 8) | patch.
      expect(formatFirmwareSemver(0x010000), '1.0.0');
      expect(formatFirmwareSemver(0x010204), '1.2.4');
      expect(formatFirmwareSemver(0x000001), '0.0.1');
    });

    test('handles zero', () {
      expect(formatFirmwareSemver(0), '0.0.0');
    });

    test('caps each component at the low byte', () {
      // High bits above the major byte are ignored; per-component cap is
      // 0xff so a corrupt/all-ones payload yields a sensible 255.255.255
      // rather than overflowing into a negative or oversized string.
      expect(formatFirmwareSemver(0xffffffff), '255.255.255');
    });
  });

  group('kAppChannel', () {
    test('is a non-empty channel string', () {
      // v1 hardcodes "dev" — switch to a --dart-define or build flavor
      // once the release flow is formalized.
      expect(kAppChannel, isNotEmpty);
    });
  });
}
