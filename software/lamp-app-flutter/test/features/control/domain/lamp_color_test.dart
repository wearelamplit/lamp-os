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
}
