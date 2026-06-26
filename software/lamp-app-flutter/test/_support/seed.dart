import 'dart:convert';
import 'dart:typed_data';

import 'in_memory_ble_client.dart';

/// Seeds every named section the lamp's page protocol serves so
/// `controlNotifierProvider`'s build() can read them via
/// `BleClient.readSection`. Used from every widget/notifier test that
/// drives the control surface.
///
/// Defaults match an out-of-the-box lamp; named overrides let individual
/// tests vary just the fields they care about. The control-password and
/// device id come from the caller because they're test-fixture identifiers,
/// not lamp state.
Future<void> seedControlBle(
  InMemoryBleClient ble, {
  required String deviceId,
  String name = 'jacko',
  int brightness = 50,
  bool advancedEnabled = false,
  int basePx = 35,
  int baseAc = 0,
  int baseBpp = 4,
  String baseColorsJson = '["#300783FF"]',
  String baseKnockoutJson = '[]',
  int shadePx = 38,
  int shadeBpp = 4,
  String shadeColorsJson = '["#000000FF"]',
  String homeSsid = '',
  int homeBrightness = 60,
  String expressionsJson = '[]',
  // Optional firmware identity fields (lamp section). Default null so
  // existing tests that don't care about the Info-tab version display
  // keep their previous, narrower CHAR_LAMP_SECTION shape.
  int? fwVersion,
  String? fwChannel,
}) async {
  // Build the firmware identity JSON tail only when both fields are set.
  // Either both should be present (post-Phase-C firmware) or both absent
  // (legacy firmware). Mixed states aren't a real-lamp scenario.
  final fwTail = (fwVersion != null && fwChannel != null)
      ? ',"fwVersion":$fwVersion,"fwChannel":"$fwChannel"'
      : '';
  ble.seedSection(
    deviceId,
    'lamp',
    Uint8List.fromList(utf8.encode(
      '{"name":"$name","brightness":$brightness,'
      '"advancedEnabled":$advancedEnabled$fwTail}',
    )),
  );
  ble.seedSection(
    deviceId,
    'base',
    Uint8List.fromList(utf8.encode(
      '{"px":$basePx,"ac":$baseAc,"bpp":$baseBpp,'
      '"colors":$baseColorsJson,"knockout":$baseKnockoutJson}',
    )),
  );
  ble.seedSection(
    deviceId,
    'shade',
    Uint8List.fromList(utf8.encode(
      '{"px":$shadePx,"bpp":$shadeBpp,"colors":$shadeColorsJson}',
    )),
  );
  ble.seedSection(
    deviceId,
    'home',
    Uint8List.fromList(utf8.encode(
      '{"ssid":"$homeSsid","brightness":$homeBrightness}',
    )),
  );
  ble.seedSection(
    deviceId,
    'expr',
    Uint8List.fromList(utf8.encode(expressionsJson)),
  );
}
