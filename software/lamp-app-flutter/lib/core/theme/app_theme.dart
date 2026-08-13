import 'package:flutter/material.dart';
import 'app_color_scheme.dart';
import 'app_spacing.dart';
import 'app_typography.dart';
import 'brand.dart';
import 'brand_extras.dart';

final ThemeData appTheme = ThemeData(
  useMaterial3: true,
  brightness: Brightness.dark,
  colorScheme: appColorScheme,
  scaffoldBackgroundColor: Brand.midnightBlack,
  textTheme: appTextTheme,
  extensions: const [BrandExtras.dark],
  cardTheme: CardThemeData(
    color: Brand.carbonGrey,
    elevation: 0,
    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(AppRadius.card)),
    margin: EdgeInsets.zero,
  ),
  bottomSheetTheme: const BottomSheetThemeData(
    showDragHandle: true,
    backgroundColor: Brand.carbonGrey,
  ),
  dialogTheme: const DialogThemeData(backgroundColor: Brand.carbonGrey),
  snackBarTheme: const SnackBarThemeData(behavior: SnackBarBehavior.floating),
  listTileTheme: const ListTileThemeData(iconColor: Brand.fogGrey),
);
