import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/brand_extras.dart';
import 'package:lamp_app/core/theme/brand.dart';

void main() {
  test('BrandExtras exposes success/gradient/warmWhite and lerps', () {
    const e = BrandExtras.dark;
    expect(e.success, Brand.deepGreen);
    expect(e.warmWhite, Brand.warmWhite);
    final mid = e.lerp(e, 0.5) as BrandExtras;
    expect(mid.success, Brand.deepGreen);
  });
}
