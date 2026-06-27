import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/theme/brand_extras.dart';
import 'package:lamp_app/core/theme/brand.dart';

void main() {
  test('appTheme is M3, dark, carries BrandExtras, themes bottom sheets', () {
    expect(appTheme.useMaterial3, true);
    expect(appTheme.brightness, Brightness.dark);
    expect(appTheme.colorScheme.primary, Brand.deepPink);
    expect(appTheme.extension<BrandExtras>(), isNotNull);
    expect(appTheme.bottomSheetTheme.showDragHandle, true);
    expect(appTheme.cardTheme.color, Brand.carbonGrey);
  });
}
