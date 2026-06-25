import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/brand_colors.dart';

void main() {
  test('brand tokens map to the design system hex values', () {
    expect(BrandColors.amberGold, const Color(0xFFE1A44A));
    expect(BrandColors.ashGrey, const Color(0xFF555555));
    expect(BrandColors.auroraBlue, const Color(0xFF446C9C));
    expect(BrandColors.cloudGrey, const Color(0xFFE0E0E0));
    expect(BrandColors.fogGrey, const Color(0xFFCCCCCC));
    expect(BrandColors.glowPink, const Color(0xFFEFA3C8));
    expect(BrandColors.lampWhite, const Color(0xFFFDFDFD));
    expect(BrandColors.lumenGreen, const Color(0xFF8DCDA6));
    expect(BrandColors.midnightBlack, const Color(0xFF0D0D0D));
    expect(BrandColors.slateGrey, const Color(0xFF888888));
    expect(BrandColors.softGrey, const Color(0xFFF5F5F5));
  });
}
