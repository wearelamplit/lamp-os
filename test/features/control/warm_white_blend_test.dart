import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/application/warm_white_blend.dart';

void main() {
  test('w=0 returns the base rgb unchanged', () {
    expect(blendWarmWhite(const Color(0xFF200040), 0), const Color(0xFF200040));
  });
  test('w>0 brightens toward warm-white (screen blend)', () {
    final base = const Color(0xFF000000);
    final out = blendWarmWhite(base, 255); // full warm white over black
    expect(out.value, const Color(0xFFFABB3E).value | 0xFF000000);
  });
  test('screen blend never darkens any channel', () {
    final base = const Color(0xFF402030);
    final out = blendWarmWhite(base, 128);
    expect(out.red, greaterThanOrEqualTo(base.red));
    expect(out.green, greaterThanOrEqualTo(base.green));
    expect(out.blue, greaterThanOrEqualTo(base.blue));
  });
}
