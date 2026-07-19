import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/sections.dart';
import 'package:lamp_app/features/lamp_shell/domain/expression_catalog.dart';

import '../../../_support/seed.dart';

ExpressionConfig _config(String type, {Map<String, int> parameters = const {}}) =>
    ExpressionConfig(
      type: type,
      enabled: true,
      colors: const [],
      intervalMin: 60,
      intervalMax: 900,
      target: 3,
      parameters: parameters,
    );

void main() {
  final catalog = ExpressionCatalog.fromJson(
      jsonDecode(defaultExprcatJson) as Map<String, dynamic>);

  test('parses the sample catalog into descriptors', () {
    expect(catalog.schemaVersion, 1);
    expect(catalog.expressions.map((e) => e.id),
        containsAll(['breathing', 'pulse', 'shifty', 'glitchy', 'spotty']));
  });

  group('Bound resolution', () {
    test('literal bound ignores pixel count', () {
      expect(const Bound.literal(60).resolve(38), 60);
    });

    test('pixels bound resolves to the pixel count', () {
      expect(const Bound.pixels().resolve(38), 38);
    });

    test('capped pixels bound clamps to the cap', () {
      expect(const Bound.pixels(cap: 10).resolve(38), 10);
      expect(const Bound.pixels(cap: 10).resolve(4), 4);
    });

    test('size default is pixel-relative, count max is capped', () {
      final breathing = catalog.byId('breathing')!;
      final size = breathing.params.firstWhere((p) => p.key == 'size');
      expect(size.def.resolve(38), 38);
      final count = breathing.params.firstWhere((p) => p.key == 'count');
      expect(count.max.resolve(38), 10);
      expect(count.def.resolve(38), 1);
    });
  });

  group('descriptor shape', () {
    test('breathing is continuous with no interval', () {
      final d = catalog.byId('breathing')!;
      expect(d.continuous, isTrue);
      expect(d.interval, isNull);
      expect(d.hasZone, isTrue);
      expect(d.zoneOptional, isTrue);
    });

    test('pausesWispOverride parses per expression', () {
      expect(catalog.byId('breathing')!.pausesWispOverride, isTrue);
      expect(catalog.byId('shifty')!.pausesWispOverride, isTrue);
      expect(catalog.byId('spotty')!.pausesWispOverride, isTrue);
      expect(catalog.byId('pulse')!.pausesWispOverride, isFalse);
      expect(catalog.byId('glitchy')!.pausesWispOverride, isFalse);
    });

    test('glitchy has an optional zone and requiresZoning params', () {
      final d = catalog.byId('glitchy')!;
      expect(d.hasZone, isTrue);
      expect(d.zoneOptional, isTrue);
      expect(d.duration!.minKey, 'durationMin');
      expect(d.duration!.maxKey, 'durationMax');
      expect(d.params.firstWhere((p) => p.key == 'count').requiresZoning,
          isTrue);
    });

    test('shifty zone is toggle-driven; fillMode options carry no zoning', () {
      final d = catalog.byId('shifty')!;
      expect(d.hasZone, isTrue);
      expect(d.zoneOptional, isTrue);
      final fill = d.params.firstWhere((p) => p.key == 'fillMode');
      expect(fill.type, ParamType.enumeration);
      expect(fill.options.every((o) => !o.zoning), isTrue);
    });

    test('interval is top-level; duration carries param keys', () {
      final d = catalog.byId('shifty')!;
      expect(d.interval!.defLo, 60);
      expect(d.interval!.defHi, 900);
      expect(d.duration!.label, 'Hold time');
    });
  });

  group('triggerCooldown', () {
    test('glitchy uses configured durationMax in ms (unit ms)', () {
      final d = catalog.byId('glitchy')!;
      expect(
        triggerCooldown(d, _config('glitchy', parameters: {'durationMax': 1500})),
        const Duration(milliseconds: 1500),
      );
    });

    test('glitchy falls back to defHi when durationMax absent', () {
      final d = catalog.byId('glitchy')!;
      expect(
        triggerCooldown(d, _config('glitchy')),
        const Duration(milliseconds: 120),
      );
    });

    test('shifty converts durationMax from seconds to ms (unit s)', () {
      final d = catalog.byId('shifty')!;
      expect(
        triggerCooldown(d, _config('shifty', parameters: {'shiftDurationMax': 5})),
        const Duration(milliseconds: 5000),
      );
    });

    test('duration-less pulse uses the fixed fallback', () {
      final d = catalog.byId('pulse')!;
      expect(
        triggerCooldown(d, _config('pulse')),
        const Duration(milliseconds: kTriggerCooldownFallbackMs),
      );
    });

    test('null descriptor uses the fixed fallback', () {
      expect(
        triggerCooldown(null, _config('anything')),
        const Duration(milliseconds: kTriggerCooldownFallbackMs),
      );
    });
  });
}
