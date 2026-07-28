import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/firmware/application/firmware_gate.dart';
import 'package:lamp_app/features/firmware/data/cached_firmware.dart';
import 'package:lamp_app/features/firmware/data/firmware_release_client.dart';

CachedFirmware _entry({
  String lampType = 'standard',
  FirmwareChannel channel = FirmwareChannel.stable,
  required int version,
}) =>
    CachedFirmware(
      lampType: lampType,
      channel: channel,
      version: version,
      byteLength: 1300000,
      fetchedAtMs: 0,
    );

Map<String, CachedFirmware> _cache(List<CachedFirmware> entries) =>
    {for (final e in entries) e.key: e};

void main() {
  group('firmwareAutoInstallTarget', () {
    // Auto-download is stable-only, so the cache holds a stable image.
    final stableCache = _cache([_entry(version: 5)]);

    test('beta lamp promoted to a newer stable entry → returns the entry', () {
      final t = firmwareAutoInstallTarget(
        connected: true,
        lampType: 'standard',
        fwVersion: 4,
        fwChannel: 'standard-beta',
        cache: stableCache,
        latchedVersion: null,
      );
      expect(t?.version, 5);
    });

    test('beta lamp promoted to stable at equal version → returns the entry',
        () {
      final t = firmwareAutoInstallTarget(
        connected: true,
        lampType: 'standard',
        fwVersion: 5,
        fwChannel: 'standard-beta',
        cache: stableCache,
        latchedVersion: null,
      );
      expect(t?.version, 5);
    });

    test('beta lamp newer than stable entry → null', () {
      expect(
        firmwareAutoInstallTarget(
          connected: true,
          lampType: 'standard',
          fwVersion: 6,
          fwChannel: 'standard-beta',
          cache: stableCache,
          latchedVersion: null,
        ),
        isNull,
      );
    });

    test('stable lamp + strictly newer stable entry → returns the entry', () {
      final t = firmwareAutoInstallTarget(
        connected: true,
        lampType: 'standard',
        fwVersion: 4,
        fwChannel: 'standard-stable',
        cache: stableCache,
        latchedVersion: null,
      );
      expect(t?.version, 5);
    });

    test('stable lamp + equal stable entry → null', () {
      expect(
        firmwareAutoInstallTarget(
          connected: true,
          lampType: 'standard',
          fwVersion: 5,
          fwChannel: 'standard-stable',
          cache: stableCache,
          latchedVersion: null,
        ),
        isNull,
      );
    });

    test('not connected → null', () {
      expect(
        firmwareAutoInstallTarget(
          connected: false,
          lampType: 'standard',
          fwVersion: 4,
          fwChannel: 'standard-beta',
          cache: stableCache,
          latchedVersion: null,
        ),
        isNull,
      );
    });

    test('lamp version unknown → null', () {
      expect(
        firmwareAutoInstallTarget(
          connected: true,
          lampType: 'standard',
          fwVersion: null,
          fwChannel: 'standard-beta',
          cache: stableCache,
          latchedVersion: null,
        ),
        isNull,
      );
    });

    test('lamp type unknown → null', () {
      expect(
        firmwareAutoInstallTarget(
          connected: true,
          lampType: null,
          fwVersion: 4,
          fwChannel: 'standard-beta',
          cache: stableCache,
          latchedVersion: null,
        ),
        isNull,
      );
    });

    test('lamp channel unknown / empty → null', () {
      expect(
        firmwareAutoInstallTarget(
          connected: true,
          lampType: 'standard',
          fwVersion: 4,
          fwChannel: '',
          cache: stableCache,
          latchedVersion: null,
        ),
        isNull,
      );
    });

    test('cache holds only a different variant → null', () {
      expect(
        firmwareAutoInstallTarget(
          connected: true,
          lampType: 'standard',
          fwVersion: 4,
          fwChannel: 'standard-beta',
          cache: _cache([_entry(lampType: 'snafu', version: 9)]),
          latchedVersion: null,
        ),
        isNull,
      );
    });

    test('empty / null cache → null', () {
      expect(
        firmwareAutoInstallTarget(
          connected: true,
          lampType: 'standard',
          fwVersion: 4,
          fwChannel: 'standard-beta',
          cache: null,
          latchedVersion: null,
        ),
        isNull,
      );
    });

    test('already latched at that version → null (fire-once)', () {
      expect(
        firmwareAutoInstallTarget(
          connected: true,
          lampType: 'standard',
          fwVersion: 4,
          fwChannel: 'standard-beta',
          cache: stableCache,
          latchedVersion: 5,
        ),
        isNull,
      );
    });
  });
}
