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
  // Optional segments JSON array for the base role, e.g.
  // '[{"name":"Base","px":35,"colors":["#300783FF"]}]'.
  String? baseSegmentsJson,
  int shadePx = 38,
  int shadeBpp = 4,
  String shadeColorsJson = '["#000000FF"]',
  // Optional segments JSON array for the shade role, e.g.
  // '[{"name":"Small Dots","px":12,"colors":["#000000FF"]},...]'.
  String? shadeSegmentsJson,
  String? lampType,
  String homeSsid = '',
  int homeBrightness = 60,
  String expressionsJson = '[]',
  // Firmware-declared expression catalog served over the `exprcat`
  // page-section. Mirrors the descriptors in
  // `software/lamp-os/src/expressions/*`. Override to test degraded /
  // alternate catalogs.
  String exprcatJson = defaultExprcatJson,
  // Optional firmware identity fields (lamp section). Default null so
  // existing tests that don't care about the Info-tab version display
  // keep their previous, narrower CHAR_LAMP_SECTION shape.
  int? fwVersion,
  String? fwChannel,
  // Whether the lamp NVS has a password set. Null = older firmware that
  // doesn't emit the field.
  bool? hasPassword,
}) async {
  // Build the firmware identity JSON tail only when both fields are set.
  // Either both should be present (post-Phase-C firmware) or both absent
  // (legacy firmware). Mixed states aren't a real-lamp scenario.
  final fwTail = (fwVersion != null && fwChannel != null)
      ? ',"fwVersion":$fwVersion,"fwChannel":"$fwChannel"'
      : '';
  final hpTail =
      hasPassword != null ? ',"hasPassword":$hasPassword' : '';
  final typeTail = lampType != null ? ',"lampType":"$lampType"' : '';
  ble.seedSection(
    deviceId,
    'lamp',
    Uint8List.fromList(utf8.encode(
      '{"name":"$name","brightness":$brightness,'
      '"advancedEnabled":$advancedEnabled$fwTail$hpTail$typeTail}',
    )),
  );
  final baseSegsTail =
      baseSegmentsJson != null ? ',"segments":$baseSegmentsJson' : '';
  ble.seedSection(
    deviceId,
    'base',
    Uint8List.fromList(utf8.encode(
      '{"px":$basePx,"ac":$baseAc,"bpp":$baseBpp,'
      '"colors":$baseColorsJson,"knockout":$baseKnockoutJson$baseSegsTail}',
    )),
  );
  final shadeSegsTail =
      shadeSegmentsJson != null ? ',"segments":$shadeSegmentsJson' : '';
  ble.seedSection(
    deviceId,
    'shade',
    Uint8List.fromList(utf8.encode(
      '{"px":$shadePx,"bpp":$shadeBpp,"colors":$shadeColorsJson$shadeSegsTail}',
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
  ble.seedSection(
    deviceId,
    'exprcat',
    Uint8List.fromList(utf8.encode(exprcatJson)),
  );
}

/// A representative catalog matching the shipped firmware descriptors, used
/// as the default for [seedControlBle]. Structural fields only — the app's
/// icons/taglines are client-side (`ExpressionPresentation`).
const defaultExprcatJson = '''
{"schemaVersion":1,"expressions":[
{"id":"breathing","name":"Breathing","continuous":true,"pausesWispOverride":true,"colors":{"max":8,"label":"Colors"},"zone":{},"params":[
{"key":"breathSpeed","type":"int","label":"Breath cycle length","min":1,"max":60,"step":1,"default":10,"unit":"s","invert":true,"leftLabel":"slow","rightLabel":"fast"},
{"key":"count","type":"int","label":"Points","min":1,"max":{"rel":"pixels","cap":10},"step":1,"default":1},
{"key":"size","type":"int","label":"Size","min":1,"max":{"rel":"pixels"},"step":1,"default":{"rel":"pixels"}},
{"key":"scatter","type":"int","label":"Spread","min":0,"max":100,"step":1,"default":0,"unit":"%","leftLabel":"Together","rightLabel":"Scattered"}]},
{"id":"pulse","name":"Pulse","continuous":false,"colors":{"max":8,"label":"Colors"},"interval":{"min":60,"max":900,"step":30,"unit":"s","default":[60,900]},"zone":{},"params":[
{"key":"pulseSpeed","type":"int","label":"Pulse speed","min":1,"max":10,"step":1,"default":3,"unit":"s","invert":true,"leftLabel":"slow","rightLabel":"fast"},
{"key":"size","type":"int","label":"Size","min":1,"max":{"rel":"pixels"},"step":1,"default":15}]},
{"id":"shifty","name":"Shifty","continuous":true,"pausesWispOverride":true,"colors":{"max":8,"label":"Colors"},"interval":{"min":60,"max":900,"step":30,"unit":"s","default":[60,900]},"duration":{"min":60,"max":1800,"step":30,"unit":"s","default":[300,600],"label":"Hold time","minKey":"shiftDurationMin","maxKey":"shiftDurationMax"},"zone":{},"params":[
{"key":"fillMode","type":"enum","label":"Fill","min":0,"max":3,"step":1,"default":0,"options":[{"value":0,"label":"Uniform"},{"value":1,"label":"Up","zoning":true},{"value":2,"label":"Down","zoning":true},{"value":3,"label":"Bloom","zoning":true}]},
{"key":"fadeDuration","type":"int","label":"Fade duration","min":10,"max":300,"step":1,"default":60,"unit":"s","leftLabel":"quick","rightLabel":"slow"}]},
{"id":"glitchy","name":"Glitchy","continuous":false,"colors":{"max":8,"label":"Colors"},"interval":{"min":60,"max":900,"step":30,"unit":"s","default":[60,900]},"duration":{"min":30,"max":2000,"step":30,"unit":"ms","default":[30,120],"label":"Glitch duration","minKey":"durationMin","maxKey":"durationMax"},"zone":{"optional":true},"params":[
{"key":"count","type":"int","label":"Points","min":1,"max":{"rel":"pixels","cap":10},"step":1,"default":1,"requiresZoning":true},
{"key":"size","type":"int","label":"Size","min":1,"max":{"rel":"pixels"},"step":1,"default":1,"requiresZoning":true}]},
{"id":"spotty","name":"Spotty","continuous":false,"pausesWispOverride":true,"colors":{"max":8,"label":"Colors"},"interval":{"min":60,"max":900,"step":30,"unit":"s","default":[60,900]},"zone":{},"params":[
{"key":"count","type":"int","label":"Points","min":1,"max":{"rel":"pixels","cap":10},"step":1,"default":3},
{"key":"size","type":"int","label":"Size","min":1,"max":{"rel":"pixels"},"step":1,"default":4},
{"key":"spotSpeed","type":"int","label":"Speed","min":1,"max":10,"step":1,"default":3,"unit":"s","invert":true,"leftLabel":"slow","rightLabel":"fast"}]}
]}
''';
