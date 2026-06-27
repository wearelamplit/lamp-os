import 'package:flutter/material.dart';
import 'brand.dart';

/// Hand-mapped M3 dark ColorScheme. Each brand color's light/dark variant
/// forms the role's tonal pair (color + container/on-color). Surfaces are
/// solid neutral tones (no opacity overlays).
const ColorScheme appColorScheme = ColorScheme(
  brightness: Brightness.dark,
  primary: Brand.deepPink,
  onPrimary: Brand.midnightBlack,
  primaryContainer: Brand.deepPink,
  onPrimaryContainer: Brand.softPink,
  secondary: Brand.goldenYellow,
  onSecondary: Brand.midnightBlack,
  secondaryContainer: Brand.goldenYellow,
  onSecondaryContainer: Brand.creamYellow,
  tertiary: Brand.deepBlue,
  onTertiary: Brand.lampWhite,
  tertiaryContainer: Brand.deepBlue,
  onTertiaryContainer: Brand.lavenderBlue,
  error: Brand.coral,
  onError: Brand.midnightBlack,
  surface: Brand.midnightBlack,
  onSurface: Brand.lampWhite,
  surfaceContainerLowest: Brand.midnightBlack,
  surfaceContainerLow: Brand.carbonGrey,
  surfaceContainer: Brand.carbonGrey,
  surfaceContainerHigh: Color(0xFF222222),
  surfaceContainerHighest: Color(0xFF2A2A2A),
  onSurfaceVariant: Brand.fogGrey,
  outline: Color(0xFF3A3A3A),
  outlineVariant: Color(0xFF2A2A2A),
);
