import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_scanner.dart';

const _lampMfgId = 0xA455;

BleAdvertisement? _parse({
  required List<int> mfg,
  String advName = 'jacko',
  String platformName = '',
  String remoteId = 'AA:BB:CC:DD:EE:FF',
  int rssi = -55,
  int? mfgId,
}) {
  return parseLampAdvertisement(
    manufacturerData: {(mfgId ?? _lampMfgId): mfg},
    advName: advName,
    platformName: platformName,
    remoteId: remoteId,
    rssi: rssi,
  );
}

void main() {
  group('FakeBleScanner', () {
    test('emits the events sent to it', () async {
      final scanner = FakeBleScanner();
      final emitted = <BleAdvertisement>[];
      final sub = scanner.results().listen(emitted.add);
      await scanner.start();
      scanner.emit(const BleAdvertisement(
        id: 'aa',
        name: 'jacko',
        serviceUuids: ['5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40'],
        baseRgb: 0x300783,
        shadeRgb: 0x000000,
        rssi: -55,
      ));
      scanner.emit(const BleAdvertisement(
        id: 'bb',
        name: 'melonie',
        serviceUuids: ['5f64f4c1-d6d9-4a44-9b3f-3a8d6f7e6b40'],
        baseRgb: 0xff0000,
        shadeRgb: 0x00ff00,
        rssi: -68,
      ));
      await Future<void>.delayed(Duration.zero);
      await sub.cancel();
      expect(emitted.map((a) => a.name).toList(), ['jacko', 'melonie']);
    });
  });

  group('parseLampAdvertisement', () {
    test('parses a 7-byte current-firmware payload', () {
      // [bR, bG, bB, sR, sG, sB, capability=0x02]
      final ad = _parse(
        mfg: [0x30, 0x07, 0x83, 0xFF, 0x00, 0x00, 0x02],
        advName: 'jacko',
      );
      expect(ad, isNotNull);
      expect(ad!.name, 'jacko');
      expect(ad.baseRgb, 0x300783);
      expect(ad.shadeRgb, 0xFF0000);
      expect(ad.isMesh, isTrue);
    });

    test('parses a 6-byte legacy v1 payload (no capability byte)', () {
      final ad = _parse(
        mfg: [0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00],
        advName: 'legacy',
      );
      expect(ad, isNotNull);
      expect(ad!.baseRgb, 0xFF0000);
      expect(ad.shadeRgb, 0x00FF00);
      expect(ad.isMesh, isFalse, reason: 'no capability byte → BT-only');
    });

    test('parses a 4-byte transitional payload (no shade)', () {
      final ad = _parse(
        mfg: [0x12, 0x34, 0x56, 0x02],
        advName: 'transitional',
      );
      expect(ad, isNotNull);
      expect(ad!.baseRgb, 0x123456);
      expect(ad.shadeRgb, 0, reason: 'no shade bytes → adv shade defaults 0');
      // Length 4 has byte 3 = 0x02 but we only read byte 6 for isMesh.
      expect(ad.isMesh, isFalse, reason: '4-byte payload < 7 bytes');
    });

    test('isMesh: bit-AND check passes for any value with bit 1 set', () {
      // Current firmware: 0x02. Future firmware might set extra bits.
      final adNewerBits = _parse(
        mfg: [0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x06], // bit 1 + bit 2
      );
      expect(adNewerBits!.isMesh, isTrue,
          reason: 'forward-compat: extra bits don\'t turn isMesh off');

      final adNoMeshBit = _parse(
        mfg: [0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x04], // only bit 2
      );
      expect(adNoMeshBit!.isMesh, isFalse,
          reason: 'capability bit 1 not set → not mesh-capable');
    });

    test('configured: reads capability bit 2 (0x04)', () {
      // Claimed lamp advertises mesh + configured (0x06).
      final claimed =
          _parse(mfg: [0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x06]);
      expect(claimed!.configured, isTrue);

      // Fresh lamp: mesh-capable but not yet set up (only bit 1).
      final fresh = _parse(mfg: [0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x02]);
      expect(fresh!.configured, isFalse,
          reason: 'bit 2 clear → unconfigured → routes to adopt wizard');

      // Legacy lamp with no capability byte → unconfigured.
      final legacy = _parse(mfg: [0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00]);
      expect(legacy!.configured, isFalse,
          reason: 'no capability byte → treated as unconfigured');
    });

    test('drops 3-byte payload (random-beacon mfg-id collision)', () {
      // The whole H3 motivation — pre-fix, 3-byte advs were parsed as
      // lamps with garbage colors.
      final ad = _parse(mfg: [0x11, 0x22, 0x33]);
      expect(ad, isNull);
    });

    test('drops 5-byte payload (unknown length, future-only firmware?)', () {
      final ad = _parse(mfg: [0x11, 0x22, 0x33, 0x44, 0x55]);
      expect(ad, isNull);
    });

    test('drops 8+ byte payload (unknown length)', () {
      final ad = _parse(mfg: [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x02, 0xAA]);
      expect(ad, isNull);
    });

    test('drops empty-name adv', () {
      final ad = _parse(
        mfg: [0x30, 0x07, 0x83, 0xFF, 0x00, 0x00, 0x02],
        advName: '',
        platformName: '',
      );
      expect(ad, isNull);
    });

    test('falls back to platformName when advName is empty', () {
      final ad = _parse(
        mfg: [0x30, 0x07, 0x83, 0xFF, 0x00, 0x00, 0x02],
        advName: '',
        platformName: 'android-platform-name',
      );
      expect(ad, isNotNull);
      expect(ad!.name, 'android-platform-name');
    });

    test('drops adv with a different manufacturer id', () {
      final ad = _parse(
        mfg: [0x30, 0x07, 0x83, 0xFF, 0x00, 0x00, 0x02],
        mfgId: 0x004C, // Apple's company id, definitely not lamp
      );
      expect(ad, isNull);
    });

    test('serviceUuids field is empty (audit L1: never read)', () {
      final ad = _parse(
        mfg: [0x30, 0x07, 0x83, 0xFF, 0x00, 0x00, 0x02],
      );
      expect(ad!.serviceUuids, isEmpty);
    });
  });
}
