import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/firmware/data/cached_firmware.dart';
import 'package:lamp_app/features/firmware/data/firmware_release_client.dart';
import 'package:lamp_app/features/firmware/presentation/firmware_update_panel.dart';

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
          lampFwVersion: null,
        ),
        FirmwareRowAction.versionUnknown,
      );
    });

    test('resolved fallback version drives the gate as if it were live', () {
      expect(
        firmwareRowActionFor(
          entry: _entry(version: 9),
          lampType: 'standard',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.install,
      );
      expect(
        firmwareRowActionFor(
          entry: _entry(version: 4),
          lampType: 'standard',
          lampFwVersion: 4,
        ),
        FirmwareRowAction.upToDate,
      );
    });
  });
}
