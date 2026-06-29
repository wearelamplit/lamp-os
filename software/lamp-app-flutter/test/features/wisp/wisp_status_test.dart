import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:lamp_app/features/wisp/domain/wisp_status.dart';
import 'package:lamp_app/features/wisp/domain/zone_source.dart';

/// Wrap a JSON string as the raw UTF-8 bytes the BLE layer hands us.
Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

void main() {
  group('WispStatus.fromBytes', () {
    test('empty bytes → empty status (not present)', () {
      final s = WispStatus.fromBytes(Uint8List(0));
      expect(s.present, isFalse);
      expect(s.wispMac, isNull);
      expect(s.currentZone, isNull);
      expect(s.zoneSource, ZoneSource.none);
      expect(s.observedZones, isEmpty);
    });

    test('"{}" bytes → empty status (not present)', () {
      final s = WispStatus.fromBytes(_b('{}'));
      expect(s.present, isFalse);
      expect(s.wispMac, isNull);
      expect(s.currentZone, isNull);
      expect(s.zoneSource, ZoneSource.none);
    });

    test('invalid UTF-8 bytes → empty status (no throw)', () {
      // 0xC3 is a UTF-8 lead byte with no continuation — utf8.decode
      // throws on this in strict mode. Parser must catch it.
      final bytes = Uint8List.fromList([0xC3, 0x28]);
      final s = WispStatus.fromBytes(bytes);
      expect(s.present, isFalse);
    });

    test('malformed JSON bytes → empty status (no throw)', () {
      final s = WispStatus.fromBytes(_b('{not json'));
      expect(s.present, isFalse);
    });

    test('non-object JSON root → empty status', () {
      // A bare array or string at the root isn't a valid wisp payload.
      final s = WispStatus.fromBytes(_b('[1,2,3]'));
      expect(s.present, isFalse);
    });

    test('canonical payload parses every field', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF",'
        '"currentZone":3,'
        '"zoneSource":"nvs",'
        '"observedZones":[0,3,7],'
        '"wifiConnected":true,'
        '"auroraConnected":true}',
      ));
      expect(s.present, isTrue);
      expect(s.wispMac, 'AA:BB:CC:DD:EE:FF');
      expect(s.currentZone, 3);
      expect(s.zoneSource, ZoneSource.nvs);
      expect(s.observedZones, [0, 3, 7]);
      expect(s.wifiConnected, isTrue);
      expect(s.auroraConnected, isTrue);
    });

    test('zoneSource "firstSeen" round-trips', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","zoneSource":"firstSeen"}',
      ));
      expect(s.zoneSource, ZoneSource.firstSeen);
    });

    test('zoneSource "appOp" round-trips', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","zoneSource":"appOp"}',
      ));
      expect(s.zoneSource, ZoneSource.appOp);
    });

    test('zoneSource "none" round-trips', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","zoneSource":"none"}',
      ));
      expect(s.zoneSource, ZoneSource.none);
    });

    test('extra unknown keys do not crash', () {
      // Forward-compat: future wisp firmware may add fields the app
      // doesn't know about. Parser must ignore them gracefully.
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF",'
        '"currentZone":1,'
        '"futureField":"whatever",'
        '"anotherFutureField":42,'
        '"nestedFuture":{"a":1,"b":[2,3]}}',
      ));
      expect(s.present, isTrue);
      expect(s.currentZone, 1);
    });

    test('missing observedZones defaults to empty list', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","currentZone":1}',
      ));
      expect(s.observedZones, isEmpty);
    });

    test('source field parses each known value', () {
      for (final pair in [
        ('off', WispSourceMode.off),
        ('manual', WispSourceMode.manual),
        ('aurora', WispSourceMode.aurora),
      ]) {
        final s = WispStatus.fromBytes(_b(
          '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"${pair.$1}"}',
        ));
        expect(s.source, pair.$2);
      }
    });

    test('missing source defaults to off', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.source, WispSourceMode.off);
    });

    test('auroraDetected is true when auroraConnected is true', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","auroraConnected":true}',
      ));
      expect(s.auroraDetected, isTrue);
    });

    test('auroraDetected is true when any zone is observed', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","observedZones":[3]}',
      ));
      expect(s.auroraDetected, isTrue);
    });

    test('auroraDetected is false when no signal', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.auroraDetected, isFalse);
    });

    test('currentPalette decodes base64-packed RGB into LampColor triples', () {
      final b64 = base64Encode([199, 0, 16, 49, 155, 0, 0, 0, 255]);
      final s = WispStatus.fromBytes(
        Uint8List.fromList(utf8.encode(
          '{"wispMac":"AA:BB:CC:DD:EE:FF","palette":"$b64"}',
        )),
      );
      expect(s.currentPalette, isNotNull);
      expect(s.currentPalette!.length, 3);
      expect(s.currentPalette![0].r, 199);
      expect(s.currentPalette![0].g, 0);
      expect(s.currentPalette![0].b, 16);
      expect(s.currentPalette![0].w, 0);
      expect(s.currentPalette![1].r, 49);
      expect(s.currentPalette![1].g, 155);
      expect(s.currentPalette![1].b, 0);
      expect(s.currentPalette![2].r, 0);
      expect(s.currentPalette![2].g, 0);
      expect(s.currentPalette![2].b, 255);
    });

    test('currentPalette absent → null', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.currentPalette, isNull);
    });

    test('currentPalette with malformed base64 → null', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","palette":"not!!base64"}',
      ));
      expect(s.currentPalette, isNull);
    });

    test('shuffleSeed parses from JSON', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","shuffleSeed":7}',
      ));
      expect(s.shuffleSeed, 7);
    });

    test('shuffleSeed defaults to 0 when absent', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.shuffleSeed, 0);
    });

    test('currentPalette truncates partial trailing triple', () {
      // 7 bytes = 2 full triples + 1 stray byte. The stray byte is
      // dropped; parser surfaces 2 colors. Defends against the wisp
      // sending an odd payload length (shouldn't happen, but the
      // parser is the defense-in-depth line if it does).
      final raw = [10, 20, 30, 40, 50, 60, 70];
      final b64 = base64Encode(raw);
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","palette":"$b64"}',
      ));
      expect(s.currentPalette, isNotNull);
      expect(s.currentPalette!.length, 2);
    });
  });
}
