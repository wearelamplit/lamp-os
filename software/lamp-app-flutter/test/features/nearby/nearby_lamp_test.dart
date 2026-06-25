import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/nearby/domain/nearby_lamp.dart';

void main() {
  group('isFactoryDefault', () {
    NearbyLamp lamp({
      String name = 'stray',
      int baseRgb = 0x300783,
      int shadeRgb = 0x000000,
    }) =>
        NearbyLamp(
          id: 'aa',
          name: name,
          rssi: -50,
          serviceUuids: const [],
          baseRgb: baseRgb,
          shadeRgb: shadeRgb,
          lastSeenEpochMs: 1,
        );

    test('true when name + base + shade all match firmware defaults', () {
      expect(lamp().isFactoryDefault, isTrue);
    });

    test('false when the user has renamed the lamp', () {
      expect(lamp(name: 'foyer').isFactoryDefault, isFalse);
    });

    test('false when the base color has been changed', () {
      expect(lamp(baseRgb: 0xff0000).isFactoryDefault, isFalse);
    });

    test('false when the shade color has been changed', () {
      expect(lamp(shadeRgb: 0xffffff).isFactoryDefault, isFalse);
    });
  });
}
