import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_typography.dart';

void main() {
  test('type scale pairs Josefin (display/title) + Inter (body)', () {
    expect(appTextTheme.displaySmall!.fontFamily, 'JosefinSans');
    expect(appTextTheme.titleMedium!.fontFamily, 'JosefinSans');
    expect(appTextTheme.bodyMedium!.fontFamily, 'Inter');
    expect(appTextTheme.labelLarge!.fontFamily, 'Inter');
    // Josefin tops out at 700
    expect(appTextTheme.displaySmall!.fontWeight!.index, lessThanOrEqualTo(FontWeight.w700.index));
  });
}
