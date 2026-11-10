import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/domain/color_rotation.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

const _devId = 'lamp-a';
const _red = LampColor(r: 0xFF, g: 0, b: 0, w: 0);
const _green = LampColor(r: 0, g: 0xFF, b: 0, w: 0);
const _blue = LampColor(r: 0, g: 0, b: 0xFF, w: 0);
const _shadeRotate = '{"on":false,"periodS":40,"dir":"down"}';
const _baseRotate = '{"on":false,"periodS":40,"dir":"up"}';

// Longer than the notifier's 60 ms write window, so a trailing write lands.
const _drain = Duration(milliseconds: 150);

// Longer than the notifier's last-seen flush delay.
const _seenFlush = Duration(milliseconds: 600);

Future<({ProviderContainer c, InMemoryBleClient ble, ControlNotifier n})>
    _build({
  String? lampType,
  String? shadeRotateJson,
  String? baseRotateJson,
}) async {
  final ble = InMemoryBleClient();
  await seedControlBle(ble,
      deviceId: _devId,
      lampType: lampType,
      shadeColorsJson: '["#FF000000","#00FF0000"]',
      baseColorsJson: '["#0000FF00","#FF000000"]',
      shadeRotateJson: shadeRotateJson,
      baseRotateJson: baseRotateJson);
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'test-lamp',
      ));
  final sub = c.listen(controlNotifierProvider(_devId), (_, _) {});
  addTearDown(() async {
    // Let the notifier's last-seen inventory write land before the container
    // goes, or it lands on a disposed inventory.
    await Future<void>.delayed(_seenFlush);
    sub.close();
    c.dispose();
  });
  await c.read(controlNotifierProvider(_devId).future);
  return (c: c, ble: ble, n: c.read(controlNotifierProvider(_devId).notifier));
}

String _last(InMemoryBleClient ble, String uuid) =>
    utf8.decode(ble.writesTo(_devId, uuid).last);

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('a lamp whose sections carry no rotate', () {
    for (final lampType in [null, 'standard']) {
      test('colour writes stay the bare array (lampType $lampType)', () async {
        final (:c, :ble, :n) = await _build(lampType: lampType);
        await n.setShadeColors(const [_red, _green]);
        await n.setBaseColors(const [_blue]);
        expect(_last(ble, BleUuids.shadeColors), '["#FF000000","#00FF0000"]');
        expect(_last(ble, BleUuids.baseColors), '["#0000FF00"]');
      });
    }

    test('setShadeRotation and setBaseRotation write nothing', () async {
      final (:c, :ble, :n) = await _build(lampType: 'standard');
      const r = ColorRotation(on: true, periodS: 30, direction: RotationDirection.up);
      await n.setShadeRotation(r);
      await n.setBaseRotation(r);
      await Future<void>.delayed(_drain);
      expect(ble.writesTo(_devId, BleUuids.shadeColors), isEmpty);
      expect(ble.writesTo(_devId, BleUuids.baseColors), isEmpty);
      final s = c.read(controlNotifierProvider(_devId)).value!;
      expect(s.shade.rotation, isNull);
      expect(s.base.rotation, isNull);
    });
  });

  group('a lamp whose sections carry rotate', () {
    for (final lampType in ['standard', 'snafu']) {
      test('colour writes take the object form (lampType $lampType)', () async {
        final (:c, :ble, :n) = await _build(
            lampType: lampType,
            shadeRotateJson: _shadeRotate,
            baseRotateJson: _baseRotate);
        await n.setShadeColors(const [_red, _green]);
        await n.setBaseColors(const [_blue, _red]);
        expect(jsonDecode(_last(ble, BleUuids.shadeColors)), {
          'colors': ['#FF000000', '#00FF0000'],
          'rotate': {'on': false, 'periodS': 40, 'dir': 'down'},
        });
        expect(jsonDecode(_last(ble, BleUuids.baseColors)), {
          'colors': ['#0000FF00', '#FF000000'],
          'rotate': {'on': false, 'periodS': 40, 'dir': 'up'},
        });
      });
    }

    test('a rotation change carries the current colours', () async {
      final (:c, :ble, :n) = await _build(baseRotateJson: _baseRotate);
      await n.setBaseRotation(const ColorRotation(
          on: true, periodS: 90, direction: RotationDirection.down));
      expect(jsonDecode(_last(ble, BleUuids.baseColors)), {
        'colors': ['#0000FF00', '#FF000000'],
        'rotate': {'on': true, 'periodS': 90, 'dir': 'down'},
      });
      expect(c.read(controlNotifierProvider(_devId)).value!.base.rotation!.periodS,
          90);
    });

    test('rotation then colours in one write window: the last write has both',
        () async {
      final (:c, :ble, :n) = await _build(shadeRotateJson: _shadeRotate);
      // Opens the window. The next two calls coalesce behind it.
      await n.setShadeColors(const [_red, _green]);
      await n.setShadeRotation(const ColorRotation(
          on: true, periodS: 25, direction: RotationDirection.up));
      await n.setShadeColors(const [_blue, _green, _red]);
      await Future<void>.delayed(_drain);
      expect(jsonDecode(_last(ble, BleUuids.shadeColors)), {
        'colors': ['#0000FF00', '#00FF0000', '#FF000000'],
        'rotate': {'on': true, 'periodS': 25, 'dir': 'up'},
      });
    });

    test('colours then rotation in one write window: the last write has both',
        () async {
      final (:c, :ble, :n) = await _build(baseRotateJson: _baseRotate);
      await n.setBaseColors(const [_red, _green]);
      await n.setBaseColors(const [_green, _blue]);
      await n.setBaseRotation(const ColorRotation(
          on: true, periodS: 5, direction: RotationDirection.down));
      await Future<void>.delayed(_drain);
      expect(jsonDecode(_last(ble, BleUuids.baseColors)), {
        'colors': ['#00FF0000', '#0000FF00'],
        'rotate': {'on': true, 'periodS': 5, 'dir': 'down'},
      });
    });

    test('the Cancel sequence ends in the opened state, on the lamp and in the app',
        () async {
      final (:c, :ble, :n) = await _build(shadeRotateJson: _shadeRotate);
      final opened = c.read(controlNotifierProvider(_devId)).value!.shade;
      await n.setShadeRotation(const ColorRotation(
          on: true, periodS: 77, direction: RotationDirection.up));
      await n.setShadeColors(const [_blue]);
      // The sheet's revert: colours, then rotation.
      await n.setShadeColors(opened.colors);
      await n.setShadeRotation(opened.rotation!);
      await Future<void>.delayed(_drain);
      final now = c.read(controlNotifierProvider(_devId)).value!.shade;
      expect(now.colors, opened.colors);
      expect(now.rotation, opened.rotation);
      expect(jsonDecode(_last(ble, BleUuids.shadeColors)), {
        'colors': ['#FF000000', '#00FF0000'],
        'rotate': {'on': false, 'periodS': 40, 'dir': 'down'},
      });
    });

    test('the longest payload (6 stops, off, 120 s, down) is 133 B, under 182 B',
        () async {
      final (:c, :ble, :n) = await _build(shadeRotateJson: _shadeRotate);
      await n.setShadeRotation(const ColorRotation(
          on: false, periodS: 120, direction: RotationDirection.down));
      await Future<void>.delayed(_drain);
      await n.setShadeColors(const [
        LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0xFF),
        LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0xFF),
        LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0xFF),
        LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0xFF),
        LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0xFF),
        LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0xFF),
      ]);
      final bytes = ble.writesTo(_devId, BleUuids.shadeColors).last;
      expect(bytes.length, 133);
      expect(bytes.length, lessThanOrEqualTo(182));
    });
  });

  test('a section rotation is read whatever lampType says', () async {
    final (:c, :ble, :n) = await _build(
        lampType: 'snafu', shadeRotateJson: '{"on":true,"periodS":40,"dir":"down"}');
    final s = c.read(controlNotifierProvider(_devId)).value!;
    expect(s.lamp.isStandard, isFalse);
    expect(s.shade.rotation, isNotNull);
    expect(s.base.rotation, isNull);
  });
}
