import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/theme/brand_colors.dart';

void main() {
  test('dark theme uses brand tokens for surface and primary', () {
    final theme = AppTheme.dark();
    expect(theme.brightness, Brightness.dark);
    expect(theme.scaffoldBackgroundColor, BrandColors.midnightBlack);
    expect(theme.colorScheme.primary, BrandColors.auroraBlue);
    expect(theme.colorScheme.secondary, BrandColors.glowPink);
    expect(theme.colorScheme.tertiary, BrandColors.lumenGreen);
    expect(theme.colorScheme.surface, BrandColors.midnightBlack);
  });
}
