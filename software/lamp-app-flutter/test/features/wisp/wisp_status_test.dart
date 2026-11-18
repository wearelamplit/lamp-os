import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/wisp/domain/wisp_config.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:lamp_app/features/wisp/domain/wisp_status.dart';

/// Wrap a JSON string as the raw UTF-8 bytes the BLE layer hands us.
Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

const _anyConfig = WispConfig(
  ledType: 'GRB',
  pixelCount: 30,
  brightness: 100,
  driftIntervalMs: 120000,
  driftFadePct: 50,
  shuffleSeed: 0,
  opSeq: 0,
);

void main() {
  group('WispStatus.fromBytes', () {
    test('empty bytes → empty status (not present)', () {
      final s = WispStatus.fromBytes(Uint8List(0));
      expect(s.present, isFalse);
      expect(s.wispMac, isNull);
      expect(s.source, WispSourceMode.off);
      expect(s.sourceLive, isFalse);
    });

    test('"{}" bytes → empty status (not present)', () {
      final s = WispStatus.fromBytes(_b('{}'));
      expect(s.present, isFalse);
      expect(s.wispMac, isNull);
    });

    test('invalid UTF-8 bytes → empty status (no throw)', () {
      // 0xC3 is a UTF-8 lead byte with no continuation; utf8.decode
      // throws in strict mode. Parser must catch it.
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
        '"source":"osc",'
        '"oscRole":"tertiary",'
        '"wifiConnected":true,'
        '"sourceLive":true}',
      ));
      expect(s.present, isTrue);
      expect(s.wispMac, 'AA:BB:CC:DD:EE:FF');
      expect(s.source, WispSourceMode.osc);
      expect(s.oscRole, OscRole.tertiary);
      expect(s.wifiConnected, isTrue);
      expect(s.sourceLive, isTrue);
    });

    test('extra unknown keys do not crash', () {
      // Forward-compat: future wisp firmware may add fields the app
      // doesn't know about. Parser must ignore them gracefully.
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF",'
        '"futureField":"whatever",'
        '"anotherFutureField":42,'
        '"nestedFuture":{"a":1,"b":[2,3]}}',
      ));
      expect(s.present, isTrue);
    });

    test('source field parses each known value', () {
      for (final pair in [
        ('off', WispSourceMode.off),
        ('manual', WispSourceMode.manual),
        ('osc', WispSourceMode.osc),
        ('aurora', WispSourceMode.off),
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

    test('sourceLive defaults to false when absent', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc"}',
      ));
      expect(s.sourceLive, isFalse);
    });

    test('oscRole defaults to primary when absent or unknown', () {
      for (final json in [
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc"}',
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc","oscRole":"bass"}',
      ]) {
        expect(WispStatus.fromBytes(_b(json)).oscRole, OscRole.primary);
      }
    });

    test('oscRole parses each role', () {
      for (final r in OscRole.values) {
        final s = WispStatus.fromBytes(_b(
          '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc",'
          '"oscRole":"${oscRoleWire(r)}"}',
        ));
        expect(s.oscRole, r);
      }
    });

    test('oscRole participates in equality and copyWith', () {
      const a = WispStatus(wispMac: 'AA:BB:CC:DD:EE:FF');
      final b = a.copyWith(oscRole: OscRole.fill);
      expect(b.oscRole, OscRole.fill);
      expect(b == a, isFalse);
      expect(b.copyWith(oscRole: OscRole.primary), a);
    });

    test('shuffleSeed defaults to 0 when absent', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.shuffleSeed, 0);
    });

    test('driftIntervalMs defaults to 120000 when absent', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.driftIntervalMs, 120000);
    });

    test('driftFadePct defaults to 50 when absent', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.driftFadePct, 50);
    });

    test('offColor parses the packed hex string form', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"off","offColor":"c8643219"}',
      ));
      expect(s.offColor.r, 200);
      expect(s.offColor.g, 100);
      expect(s.offColor.b, 50);
      expect(s.offColor.w, 25);
    });

    // The wisp emits offColor only in Off mode.
    test('a frame without offColor reads the default Off colour', () {
      final manual = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}',
      ));
      expect(manual.offColor, WispStatus.empty.offColor);
    });

    test('name parses from JSON', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","name":"living room"}',
      ));
      expect(s.name, 'living room');
    });

    test('name defaults to empty string when absent', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.name, '');
    });

    test('hasPassword parses true from JSON', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","hasPassword":true}',
      ));
      expect(s.hasPassword, isTrue);
    });

    test('hasPassword defaults to false when absent', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.hasPassword, isFalse);
    });

    test('hasPassword parses false from JSON', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","hasPassword":false}',
      ));
      expect(s.hasPassword, isFalse);
    });

    test('brightness rides withConfig, copyWith and ==', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","configSplit":true}',
      )).withConfig(_anyConfig.copyWith(brightness: 40));
      expect(s.brightness, 40);
      final dimmer = s.copyWith(brightness: 20);
      expect(dimmer.brightness, 20);
      expect(dimmer == s, isFalse);
      expect(s.copyWith(brightness: 40), s);
    });

    test('brightness defaults to 100 (full) when absent', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.brightness, 100);
    });

    test('wifiChannelMismatch + wifiApChannel parse from JSON', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","wifiConnected":false,'
        '"wifiChannelMismatch":true,"wifiApChannel":1}',
      ));
      expect(s.wifiChannelMismatch, isTrue);
      expect(s.wifiApChannel, 1);
      expect(s.wifiConnected, isFalse);
    });

    test('wifiChannelMismatch defaults false and wifiApChannel 0 when absent',
        () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF"}',
      ));
      expect(s.wifiChannelMismatch, isFalse);
      expect(s.wifiApChannel, 0);
    });

    test('ethIp parses from JSON and defaults empty when absent', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","ethIp":"10.0.0.42"}',
      ));
      expect(s.ethIp, '10.0.0.42');
      expect(WispStatus.fromBytes(_b('{"wispMac":"AA:BB:CC:DD:EE:FF"}')).ethIp,
          '');
    });
  });

  group('WispStatus config knowledge', () {
    // Settings come only from the wisp's config report ([withConfig]).
    test('a status without configSplit carries no settings', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc","ledType":"BGR",'
        '"px":12,"brightness":30,"driftIntervalMs":5000,"driftFadePct":5,'
        '"shuffleSeed":3,"opSeq":2}',
      ));
      const none = WispStatus.empty;
      expect(s.configKnown, isFalse);
      expect(s.fromWisp, isFalse);
      expect(s.ledType, none.ledType);
      expect(s.pixelCount, none.pixelCount);
      expect(s.brightness, none.brightness);
      expect(s.driftIntervalMs, none.driftIntervalMs);
      expect(s.driftFadePct, none.driftFadePct);
      expect(s.shuffleSeed, none.shuffleSeed);
      expect(s.opSeq, none.opSeq);
    });

    test('a status with configSplit is from the wisp', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc","configSplit":true}',
      ));
      expect(s.fromWisp, isTrue);
    });

    // Hello-only merge: the lamp has no wispStatus payload yet, so the
    // config fields are parser defaults, not the wisp's settings.
    test('no wisp payload (no source) → config unknown', () {
      final s = WispStatus.fromBytes(_b('{"wispMac":"AA:BB:CC:DD:EE:FF"}'));
      expect(s.present, isTrue);
      expect(s.configKnown, isFalse);
    });

    test('configSplit marks the status config-free → unknown', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc","configSplit":true}',
      ));
      expect(s.configKnown, isFalse);
    });

    test('configLastSeenMs parses; absent → null', () {
      expect(
        WispStatus.fromBytes(_b(
          '{"wispMac":"AA:BB:CC:DD:EE:FF","configLastSeenMs":1234}',
        )).configLastSeenMs,
        1234,
      );
      expect(
        WispStatus.fromBytes(_b('{"wispMac":"AA:BB:CC:DD:EE:FF"}'))
            .configLastSeenMs,
        isNull,
      );
    });

    test('withConfig overlays every config field and marks it known', () {
      const config = WispConfig(
        ledType: 'BGR',
        pixelCount: 12,
        brightness: 40,
        driftIntervalMs: 60000,
        driftFadePct: 10,
        shuffleSeed: 9,
        opSeq: 5,
      );
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","configSplit":true,"source":"off"}',
      )).withConfig(config);
      expect(s.configKnown, isTrue);
      expect(s.ledType, 'BGR');
      expect(s.pixelCount, 12);
      expect(s.brightness, 40);
      expect(s.driftIntervalMs, 60000);
      expect(s.driftFadePct, 10);
      expect(s.shuffleSeed, 9);
      expect(s.opSeq, 5);
      expect(s.source, WispSourceMode.off);
    });

    test('copyWith keeps config knowledge', () {
      final s = WispStatus.fromBytes(_b(
        '{"wispMac":"AA:BB:CC:DD:EE:FF","source":"osc","configSplit":true,'
        '"configLastSeenMs":7}',
      )).withConfig(_anyConfig).copyWith(brightness: 30);
      expect(s.configKnown, isTrue);
      expect(s.configLastSeenMs, 7);
    });
  });
}
