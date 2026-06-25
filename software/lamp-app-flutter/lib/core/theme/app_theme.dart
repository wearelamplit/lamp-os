import 'package:flutter/material.dart';

import 'brand_colors.dart';

abstract class AppTheme {
  static ThemeData dark() {
    const colorScheme = ColorScheme.dark(
      primary: BrandColors.auroraBlue,
      onPrimary: BrandColors.lampWhite,
      secondary: BrandColors.glowPink,
      onSecondary: BrandColors.midnightBlack,
      tertiary: BrandColors.lumenGreen,
      onTertiary: BrandColors.midnightBlack,
      surface: BrandColors.midnightBlack,
      onSurface: BrandColors.lampWhite,
      error: BrandColors.error,
      onError: BrandColors.lampWhite,
    );

    return ThemeData(
      useMaterial3: true,
      brightness: Brightness.dark,
      colorScheme: colorScheme,
      scaffoldBackgroundColor: BrandColors.midnightBlack,
      canvasColor: BrandColors.midnightBlack,
      cardTheme: CardThemeData(
        color: BrandColors.lampWhite.withValues(alpha: 0.04),
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: const BorderRadius.all(Radius.circular(12)),
          side: BorderSide(
              color: BrandColors.lampWhite.withValues(alpha: 0.06)),
        ),
      ),
      dividerTheme: DividerThemeData(
        color: BrandColors.lampWhite.withValues(alpha: 0.08),
        thickness: 1,
      ),
      // Inputs: solid border + yellow floating label, ported from the
      // Vue app's `Text.vue` / `Number.vue` / `Field.vue` (see
      // `cool-cvolor-picker-scalable-shannon.md` notes).
      inputDecorationTheme: const InputDecorationTheme(
        border: OutlineInputBorder(
          borderRadius: BorderRadius.all(Radius.circular(10)),
          borderSide: BorderSide(color: BrandColors.inputBorder, width: 1.5),
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.all(Radius.circular(10)),
          borderSide: BorderSide(color: BrandColors.inputBorder, width: 1.5),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.all(Radius.circular(10)),
          borderSide: BorderSide(color: BrandColors.auroraBlue, width: 1.5),
        ),
        errorBorder: OutlineInputBorder(
          borderRadius: BorderRadius.all(Radius.circular(10)),
          borderSide: BorderSide(color: BrandColors.error, width: 1.5),
        ),
        focusedErrorBorder: OutlineInputBorder(
          borderRadius: BorderRadius.all(Radius.circular(10)),
          borderSide: BorderSide(color: BrandColors.error, width: 1.5),
        ),
        labelStyle: TextStyle(
          color: BrandColors.headerYellow,
          fontWeight: FontWeight.w500,
          fontSize: 13,
        ),
        floatingLabelStyle: TextStyle(
          color: BrandColors.headerYellow,
          fontWeight: FontWeight.w600,
          fontSize: 13,
        ),
        hintStyle: TextStyle(color: BrandColors.slateGrey),
      ),
      sliderTheme: const SliderThemeData(
        thumbColor: BrandColors.lampWhite,
        activeTrackColor: BrandColors.glowPink,
        inactiveTrackColor: BrandColors.ashGrey,
        // 12-px radius → 24-px diameter — slightly larger than Material's
        // default 10 so the thumb feels more present without overpowering
        // the track (Vue uses 36, too large for a phone in M3).
        thumbShape: RoundSliderThumbShape(
          enabledThumbRadius: 12,
          elevation: 4,
          pressedElevation: 8,
        ),
      ),
      textTheme: const TextTheme(
        bodyLarge: TextStyle(color: BrandColors.lampWhite),
        bodyMedium: TextStyle(color: BrandColors.fogGrey),
        labelSmall: TextStyle(
          color: BrandColors.slateGrey,
          letterSpacing: 0.05,
        ),
      ),
    );
  }
}
