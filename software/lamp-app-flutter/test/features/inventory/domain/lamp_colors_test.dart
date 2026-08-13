import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/inventory/domain/lamp_colors.dart';
import 'package:lamp_app/features/nearby/domain/nearby_lamp.dart';

NearbyLamp nearbyWith({required int baseRgb, required int shadeRgb}) =>
    NearbyLamp(
      id: 'lamp-1',
      name: 'lamp',
      rssi: -60,
      serviceUuids: const [],
      baseRgb: baseRgb,
      shadeRgb: shadeRgb,
      lastSeenEpochMs: 0,
    );

InventoryLamp invWith({List<int>? lastBaseColor, List<int>? lastShadeColor}) =>
    InventoryLamp(
      id: 'lamp-1',
      name: 'lamp',
      lastBaseColor: lastBaseColor,
      lastShadeColor: lastShadeColor,
    );

void main() {
  group('resolveLampColors base', () {
    test('adv baseRgb == 0 with cached lastBaseColor falls back to cache', () {
      final near = nearbyWith(baseRgb: 0, shadeRgb: 0xFF0000);
      final inv = invWith(lastBaseColor: const [10, 20, 30, 200]);

      final colors = resolveLampColors(inv: inv, near: near);

      final expected = const LampColor(r: 10, g: 20, b: 30, w: 200).toSwatch();
      expect(colors.base, expected);
      expect(colors.base, isNot(const Color.fromARGB(0xFF, 0, 0, 0)));
    });

    test('adv baseRgb == 0 with no cache returns null', () {
      final near = nearbyWith(baseRgb: 0, shadeRgb: 0xFF0000);
      final inv = invWith();

      final colors = resolveLampColors(inv: inv, near: near);

      expect(colors.base, isNull);
    });

    test('adv baseRgb != 0 uses the live adv color', () {
      final near = nearbyWith(baseRgb: 0x102030, shadeRgb: 0xFF0000);
      final inv = invWith(lastBaseColor: const [10, 20, 30, 200]);

      final colors = resolveLampColors(inv: inv, near: near);

      final expected =
          const LampColor(r: 0x10, g: 0x20, b: 0x30, w: 200).toSwatch();
      expect(colors.base, expected);
    });
  });

  group('resolveLampColors shade (regression)', () {
    test('adv shadeRgb == 0 with cached lastShadeColor falls back to cache',
        () {
      final near = nearbyWith(baseRgb: 0x102030, shadeRgb: 0);
      final inv = invWith(lastShadeColor: const [1, 2, 3, 4]);

      final colors = resolveLampColors(inv: inv, near: near);

      final expected = const LampColor(r: 1, g: 2, b: 3, w: 4).toSwatch();
      expect(colors.shade, expected);
    });

    test('adv shadeRgb != 0 uses the live adv color', () {
      final near = nearbyWith(baseRgb: 0x102030, shadeRgb: 0x0A0B0C);
      final inv = invWith(lastShadeColor: const [1, 2, 3, 4]);

      final colors = resolveLampColors(inv: inv, near: near);

      final expected =
          const LampColor(r: 0x0A, g: 0x0B, b: 0x0C, w: 4).toSwatch();
      expect(colors.shade, expected);
    });
  });
}
