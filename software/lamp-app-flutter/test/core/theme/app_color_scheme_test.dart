import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_color_scheme.dart';
import 'package:lamp_app/core/theme/brand.dart';

void main() {
  test('color scheme maps brand roles correctly', () {
    expect(appColorScheme.brightness, Brightness.dark);
    expect(appColorScheme.primary, Brand.deepPink);
    expect(appColorScheme.secondary, Brand.goldenYellow);
    expect(appColorScheme.tertiary, Brand.deepBlue);
    expect(appColorScheme.error, Brand.coral);
    expect(appColorScheme.surface, Brand.midnightBlack);
    expect(appColorScheme.surfaceContainer, Brand.carbonGrey);
    expect(appColorScheme.onSurface, Brand.lampWhite);
    expect(appColorScheme.onSurfaceVariant, Brand.fogGrey);
  });
}
