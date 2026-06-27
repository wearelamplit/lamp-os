import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/brand.dart';

void main() {
  test('brand tokens match lamplit.ca/brand hex values', () {
    expect(Brand.deepPink, const Color(0xFFC869C8));
    expect(Brand.goldenYellow, const Color(0xFFF8CC48));
    expect(Brand.deepBlue, const Color(0xFF6366F1));
    expect(Brand.deepGreen, const Color(0xFF86EFAC));
    expect(Brand.midnightBlack, const Color(0xFF0D0D0D));
    expect(Brand.carbonGrey, const Color(0xFF1A1A1A));
    expect(Brand.lampWhite, const Color(0xFFFDFDFD));
    expect(Brand.coral, const Color(0xFFF87171));
    expect(Brand.warmWhite, const Color(0xFFFABB3E));
  });
}
