import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/control/domain/sections.dart';
import 'package:lamp_app/features/social/domain/social_mode.dart';

void main() {
  test('LampSection parses brightness + name', () {
    final s = LampSection.fromJson(jsonDecode(
      '{"name":"jacko","brightness":42,"advancedEnabled":false}',
    ) as Map<String, dynamic>);
    expect(s.name, 'jacko');
    expect(s.brightness, 42);
  });

  test('LampSection parses fwVersion + fwChannel when emitted', () {
    final s = LampSection.fromJson(jsonDecode(
      '{"name":"jacko","brightness":42,"advancedEnabled":false,'
      '"fwVersion":65536,"fwChannel":"stable"}',
    ) as Map<String, dynamic>);
    expect(s.fwVersion, 0x010000);
    expect(s.fwChannel, 'stable');
  });

  test('LampSection.fwVersion + fwChannel null on legacy firmware', () {
    // Old firmware that doesn't yet emit fwVersion/fwChannel — the Info
    // tab renders these as "..." rather than crashing on a null cast.
    final s = LampSection.fromJson(jsonDecode(
      '{"name":"jacko","brightness":42,"advancedEnabled":false}',
    ) as Map<String, dynamic>);
    expect(s.fwVersion, isNull);
    expect(s.fwChannel, isNull);
  });

  test('BaseSection parses colors, ac, px', () {
    final s = BaseSection.fromJson(jsonDecode(
      '{"px":35,"ac":1,"bpp":4,"colors":["#300783FF","#FF0000AA"],"knockout":[]}',
    ) as Map<String, dynamic>);
    expect(s.px, 35);
    expect(s.ac, 1);
    expect(s.colors.length, 2);
    expect(s.colors[0], const LampColor(r: 0x30, g: 0x07, b: 0x83, w: 0xFF));
    expect(s.colors[1].w, 0xAA);
  });

  test('BaseSection.knockout is empty when the JSON omits it', () {
    final s = BaseSection.fromJson(jsonDecode(
      '{"px":35,"ac":0,"bpp":4,"colors":[]}',
    ) as Map<String, dynamic>);
    expect(s.knockout, isEmpty);
  });

  test('BaseSection.knockout folds entries into a map', () {
    // Positional knockout: index = pixel, value = brightness %. Default
    // (100) entries are skipped on parse, so only non-defaults end up in
    // the map.
    final s = BaseSection.fromJson(jsonDecode(
      '{"px":35,"ac":0,"bpp":4,"colors":[],"knockout":[100,100,100,50,100,100,100,25,100,100]}',
    ) as Map<String, dynamic>);
    expect(s.knockout, {3: 50, 7: 25});
  });

  test('ShadeSection parses single color', () {
    final s = ShadeSection.fromJson(jsonDecode(
      '{"px":38,"bpp":4,"colors":["#000000FF"]}',
    ) as Map<String, dynamic>);
    expect(s.colors.single.w, 0xFF);
  });

  test('HomeSection parses ssid + brightness (legacy password field ignored)',
      () {
    // Legacy lamps wrote a "password" field — we silently ignore it now.
    final s = HomeSection.fromJson(jsonDecode(
      '{"ssid":"home","password":"********","brightness":40}',
    ) as Map<String, dynamic>);
    expect(s.ssid, 'home');
    expect(s.brightness, 40);
  });

  test('HomeSection defaults on a sparse JSON', () {
    final s = HomeSection.fromJson(<String, dynamic>{});
    expect(s.ssid, '');
    expect(s.brightness, 60);
  });

  test('ExpressionConfig round-trips through toJson + fromJson', () {
    final original = ExpressionConfig(
      type: 'glitchy',
      enabled: true,
      colors: [LampColor.fromHex('#FF00FFAA')],
      intervalMin: 30,
      intervalMax: 120,
      target: 2,
      parameters: {'flickerRate': 5, 'jitter': 100},
    );
    final round = ExpressionConfig.fromJson(
      Map<String, dynamic>.from(
          jsonDecode(jsonEncode(original.toJson())) as Map),
    );
    expect(round.type, 'glitchy');
    expect(round.enabled, isTrue);
    expect(round.colors.single.toHex(), '#FF00FFAA');
    expect(round.intervalMin, 30);
    expect(round.intervalMax, 120);
    expect(round.target, 2);
    expect(round.parameters, {'flickerRate': 5, 'jitter': 100});
  });

  test('ExpressionsSection parses an empty array', () {
    expect(ExpressionsSection.fromJson([]).expressions, isEmpty);
  });

  test('ExpressionsSection parses two entries', () {
    final s = ExpressionsSection.fromJson(
      (jsonDecode(
        '[{"type":"breathing","enabled":true,"colors":[],"intervalMin":10,"intervalMax":20,"target":1},'
        '{"type":"glitchy","enabled":false,"colors":[],"intervalMin":60,"intervalMax":900,"target":3}]',
      ) as List).cast<Map<String, dynamic>>(),
    );
    expect(s.expressions, hasLength(2));
    expect(s.expressions[0].type, 'breathing');
    expect(s.expressions[1].target, 3);
  });

  group('ExpressionConfig drops legacy disabledDuringWispOverride', () {
    // The field is no longer a per-instance config — it's now a pure
    // type-property looked up via ExpressionTypeMeta. fromJson/toJson must
    // tolerate the legacy key (older firmware/payloads may still ship it)
    // but never surface it as a parameter or carry it forward.
    test('legacy disabledDuringWispOverride is dropped, not stored as param',
        () {
      final e = ExpressionConfig.fromJson(<String, dynamic>{
        'type': 'breathing',
        'enabled': true,
        'disabledDuringWispOverride': true,
        'someRealParam': 42,
      });
      expect(e.type, 'breathing');
      expect(e.parameters.containsKey('disabledDuringWispOverride'), isFalse);
      expect(e.parameters['someRealParam'], 42);
    });
    test('toJson does not emit disabledDuringWispOverride', () {
      const e = ExpressionConfig(
        type: 'breathing',
        enabled: true,
        colors: [],
        intervalMin: 60,
        intervalMax: 900,
        target: 3,
        parameters: {},
      );
      expect(e.toJson().containsKey('disabledDuringWispOverride'), isFalse);
    });
  });

  group('value-class equality', () {
    // The point of overriding ==/hashCode on these is so Riverpod's
    // `.select` and AsyncValue equality can short-circuit on unchanged
    // sections. The tests pin that behaviour against accidental
    // regression.

    test('LampSection equal-by-field but distinct instances are ==', () {
      // Build the second instance via fromJson so dart's const-literal
      // canonicalisation doesn't make `identical(a, b)` true and mask a
      // missing ==/hashCode override.
      const a = LampSection(
        name: 'a',
        brightness: 100,
        advancedEnabled: false,
        webappEnabled: true,
        socialMode: SocialMode.ambivert,
        fwVersion: 1,
        fwChannel: 'stable',
      );
      final b = LampSection.fromJson(<String, dynamic>{
        'name': 'a',
        'brightness': 100,
        'advancedEnabled': false,
        'socialMode': SocialMode.ambivert.wire,
        'fwVersion': 1,
        'fwChannel': 'stable',
      });
      expect(identical(a, b), isFalse);
      expect(a, equals(b));
      expect(a.hashCode, b.hashCode);
    });

    test('BaseSection equality uses deep colors + knockout', () {
      // Build with mutable lists/maps so Dart's const-literal
      // canonicalisation can't make `identical(a, b)` true and mask a
      // missing == override.
      // ignore: prefer_const_constructors
      final a = BaseSection(
        px: 35,
        ac: 0,
        bpp: 4,
        byteOrder: 'GRBW',
        colors: [const LampColor(r: 255, g: 0, b: 0, w: 0)],
        knockout: {1: 50, 7: 75},
      );
      // ignore: prefer_const_constructors
      final b = BaseSection(
        px: 35,
        ac: 0,
        bpp: 4,
        byteOrder: 'GRBW',
        colors: [const LampColor(r: 255, g: 0, b: 0, w: 0)],
        knockout: {1: 50, 7: 75},
      );
      expect(identical(a, b), isFalse);
      expect(a, equals(b));
      expect(a.hashCode, b.hashCode);

      // Mutating one slot in the knockout map should produce inequality.
      // ignore: prefer_const_constructors
      final c = BaseSection(
        px: 35,
        ac: 0,
        bpp: 4,
        byteOrder: 'GRBW',
        colors: [const LampColor(r: 255, g: 0, b: 0, w: 0)],
        knockout: {1: 50, 7: 80},
      );
      expect(a, isNot(equals(c)));
    });

    test('ExpressionConfig equality uses deep parameters map', () {
      const a = ExpressionConfig(
        type: 'breathing',
        enabled: true,
        colors: [LampColor(r: 1, g: 2, b: 3, w: 0)],
        intervalMin: 60,
        intervalMax: 900,
        target: 3,
        parameters: {'rate': 5, 'depth': 10},
      );
      const b = ExpressionConfig(
        type: 'breathing',
        enabled: true,
        colors: [LampColor(r: 1, g: 2, b: 3, w: 0)],
        intervalMin: 60,
        intervalMax: 900,
        target: 3,
        parameters: {'rate': 5, 'depth': 10},
      );
      expect(a, equals(b));
      expect(a.hashCode, b.hashCode);
    });
  });
}
