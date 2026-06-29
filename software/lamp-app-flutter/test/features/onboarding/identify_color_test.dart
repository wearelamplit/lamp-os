import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/onboarding/application/identify_color.dart';

void main() {
  test('washedOutBright lightens each channel toward white, never darker', () {
    const base = LampColor(r: 0x20, g: 0x00, b: 0x40, w: 0);
    final out = washedOutBright(base);
    expect(out.r, greaterThan(base.r));
    expect(out.g, greaterThan(base.g));
    expect(out.b, greaterThan(base.b));
    expect(out.r, lessThanOrEqualTo(255));
  });
  test('pure white stays white', () {
    const white = LampColor(r: 255, g: 255, b: 255, w: 0);
    final out = washedOutBright(white);
    expect(out.r, 255); expect(out.g, 255); expect(out.b, 255);
  });
}
