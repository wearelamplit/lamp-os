import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';

void main() {
  test('parses #RRGGBBWW into r/g/b/w', () {
    final c = LampColor.fromHex('#30FF80AA');
    expect(c.r, 0x30);
    expect(c.g, 0xFF);
    expect(c.b, 0x80);
    expect(c.w, 0xAA);
  });

  test('accepts hex without leading hash', () {
    expect(LampColor.fromHex('300783FF').w, 0xFF);
  });

  test('toHex round-trips exactly, upper-case, with #', () {
    const c = LampColor(r: 0x30, g: 0x07, b: 0x83, w: 0xFF);
    expect(c.toHex(), '#300783FF');
  });

  test('throws FormatException on invalid hex length', () {
    expect(() => LampColor.fromHex('#FFF'), throwsFormatException);
  });

  test('withRgb keeps the W byte', () {
    const c = LampColor(r: 0x10, g: 0x20, b: 0x30, w: 0xAA);
    final next = c.withRgb(r: 0xFF, g: 0xFF, b: 0xFF);
    expect(next.w, 0xAA);
    expect(next.r, 0xFF);
  });

  // Pinned to the same vectors as the firmware's blendedIdentity native test.
  group('blendedIdentity', () {
    const red = LampColor(r: 255, g: 0, b: 0, w: 0);
    const blue = LampColor(r: 0, g: 0, b: 255, w: 0);
    const cyan = LampColor(r: 0, g: 255, b: 255, w: 0);

    test('single stop is the identity', () {
      const c = LampColor(r: 10, g: 20, b: 30, w: 200);
      expect(LampColor.blendedIdentity([c]), c);
    });

    test('2R + 1B leans red', () {
      final out = LampColor.blendedIdentity([red, red, blue]);
      expect(out.r > out.b, isTrue);
      expect(out.g, 0);
    });

    test('complementary pair guard avoids grey', () {
      final out = LampColor.blendedIdentity([red, cyan]);
      expect(out.r > out.g, isTrue);
      expect(out.r > out.b, isTrue);
    });

    test('white channel passes through identical stops', () {
      const c = LampColor(r: 10, g: 20, b: 30, w: 200);
      final out = LampColor.blendedIdentity([c, c]);
      expect(out.w, 200);
      expect(out.r, 10);
      expect(out.g, 20);
      expect(out.b, 30);
    });

    test('empty is black', () {
      expect(LampColor.blendedIdentity(const []), LampColor.black);
    });
  });
}
