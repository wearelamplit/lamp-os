import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/firmware/application/firmware_gate.dart';
import 'package:lamp_app/features/firmware/data/cached_firmware.dart';
import 'package:lamp_app/features/firmware/data/firmware_release_client.dart';

CachedFirmware _entry({
  String lampType = 'standard',
  FirmwareChannel channel = FirmwareChannel.beta,
  required int version,
}) =>
    CachedFirmware(
      lampType: lampType,
      channel: channel,
      version: version,
      byteLength: 1300000,
      fetchedAtMs: 0,
    );

void main() {
  group('firmwareRowActionFor', () {
    test('variant matches and cached build is newer → install', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(version: 5),
          lampType: 'standard',
          lampChannel: 'standard-beta',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.install,
      );
    });

    test('variant matches and cached build equal → up to date', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(version: 4),
          lampType: 'standard',
          lampChannel: 'standard-beta',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.upToDate,
      );
    });

    test('variant matches and cached build older → up to date', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(version: 3),
          lampType: 'standard',
          lampChannel: 'standard-beta',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.upToDate,
      );
    });

    test('variant differs → not this lamp', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(lampType: 'snafu', version: 9),
          lampType: 'standard',
          lampChannel: 'standard-beta',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.notThisLamp,
      );
    });

    test('variant matches but lamp version unknown → version unknown', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(version: 9),
          lampType: 'standard',
          lampChannel: 'standard-beta',
          lampFwVersion: null,
        ),
        FirmwareRowAction.versionUnknown,
      );
    });

    test('lamp channel unknown → version unknown', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(version: 9),
          lampType: 'standard',
          lampChannel: null,
          lampFwVersion: 4,
        ),
        FirmwareRowAction.versionUnknown,
      );
    });

    test('beta lamp + stable entry at equal version → install (promotion)', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(channel: FirmwareChannel.stable, version: 4),
          lampType: 'standard',
          lampChannel: 'standard-beta',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.install,
      );
    });

    test('stable lamp + beta entry → up to date (no reverse promotion)', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(channel: FirmwareChannel.beta, version: 5),
          lampType: 'standard',
          lampChannel: 'standard-stable',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.upToDate,
      );
    });

    test('resolved fallback version drives the gate as if it were live', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(version: 9),
          lampType: 'standard',
          lampChannel: 'standard-beta',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.install,
      );
      expect(
        firmwareRowActionFor(
          entry: _entry(version: 4),
          lampType: 'standard',
          lampChannel: 'standard-beta',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.upToDate,
      );
    });
  });
}
