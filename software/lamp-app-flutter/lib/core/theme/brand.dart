import 'package:flutter/material.dart';

/// Raw brand tokens. The ONLY place color hex literals live.
/// Source of truth: lamplit.ca/brand. Recolor the app here.
abstract class Brand {
  // Primary (Pink)
  static const softPink = Color(0xFFEFA8F0);
  static const deepPink = Color(0xFFC869C8);
  // Secondary (Yellow)
  static const creamYellow = Color(0xFFFFFDD1);
  static const goldenYellow = Color(0xFFF8CC48);
  // Tertiary (Blue)
  static const lavenderBlue = Color(0xFF9EA1FF);
  static const deepBlue = Color(0xFF6366F1);
  // Quaternary (Green) → success role
  static const lightGreen = Color(0xFFBBFFAD);
  static const deepGreen = Color(0xFF86EFAC);
  // Neutrals
  static const midnightBlack = Color(0xFF0D0D0D);
  static const carbonGrey = Color(0xFF1A1A1A);
  static const lampWhite = Color(0xFFFDFDFD);
  static const fogGrey = Color(0xFFCCCCCC);
  // Functional
  static const coral = Color(0xFFF87171);
  static const warmWhite = Color(0xFFFABB3E);
}
