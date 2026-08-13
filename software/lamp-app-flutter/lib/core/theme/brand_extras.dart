import 'package:flutter/material.dart';
import 'brand.dart';

/// Roles M3's ColorScheme has no slot for: success (green), the aurora→pink
/// chrome gradient, and the warm-white channel reference.
@immutable
class BrandExtras extends ThemeExtension<BrandExtras> {
  const BrandExtras({
    required this.success,
    required this.onSuccess,
    required this.chromeGradient,
    required this.warmWhite,
  });

  final Color success;
  final Color onSuccess;
  final Gradient chromeGradient;
  final Color warmWhite;

  static const BrandExtras dark = BrandExtras(
    success: Brand.deepGreen,
    onSuccess: Brand.midnightBlack,
    chromeGradient: LinearGradient(colors: [Brand.deepBlue, Brand.deepPink]),
    warmWhite: Brand.warmWhite,
  );

  @override
  BrandExtras copyWith({
    Color? success,
    Color? onSuccess,
    Gradient? chromeGradient,
    Color? warmWhite,
  }) =>
      BrandExtras(
        success: success ?? this.success,
        onSuccess: onSuccess ?? this.onSuccess,
        chromeGradient: chromeGradient ?? this.chromeGradient,
        warmWhite: warmWhite ?? this.warmWhite,
      );

  @override
  BrandExtras lerp(ThemeExtension<BrandExtras>? other, double t) {
    if (other is! BrandExtras) return this;
    return BrandExtras(
      success: Color.lerp(success, other.success, t)!,
      onSuccess: Color.lerp(onSuccess, other.onSuccess, t)!,
      chromeGradient: Gradient.lerp(chromeGradient, other.chromeGradient, t)!,
      warmWhite: Color.lerp(warmWhite, other.warmWhite, t)!,
    );
  }
}

extension BrandExtrasContext on BuildContext {
  BrandExtras get brandExtras =>
      Theme.of(this).extension<BrandExtras>() ?? BrandExtras.dark;
}
