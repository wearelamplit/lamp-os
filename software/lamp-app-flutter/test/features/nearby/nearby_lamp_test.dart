import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/nearby/domain/nearby_lamp.dart';

void main() {
  group('isFactoryDefault (driven by the advertised `configured` bit)', () {
    NearbyLamp lamp({
      bool configured = false,
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
          configured: configured,
        );

    test('true when the lamp has not been set up', () {
      expect(lamp(configured: false).isFactoryDefault, isTrue);
    });

    test('defaults to factory-default when the bit is absent', () {
      expect(lamp().isFactoryDefault, isTrue);
    });

    test('false once the lamp is configured', () {
      expect(lamp(configured: true).isFactoryDefault, isFalse);
    });

    test('name and colors no longer affect the verdict', () {
      // A custom-looking but unclaimed lamp is still factory-default
      // (the old color-match could never recognise this case)…
      expect(
        lamp(configured: false, name: 'foyer', baseRgb: 0xff0000)
            .isFactoryDefault,
        isTrue,
      );
      // …and a default-looking but claimed lamp is not.
      expect(
        lamp(configured: true, name: 'stray', baseRgb: 0x300783)
            .isFactoryDefault,
        isFalse,
      );
    });
  });
}
