import 'package:flutter/material.dart';
import 'brand.dart';

const _josefin = 'JosefinSans';
const _inter = 'Inter';
const _ink = Brand.lampWhite;

/// Type scale. Josefin Sans for brand/headings/titles, Inter for dense body
/// + data. Screens use context.textTheme.* instead of inline TextStyle.
const TextTheme appTextTheme = TextTheme(
  displaySmall: TextStyle(fontFamily: _josefin, fontWeight: FontWeight.w700, fontSize: 28, color: _ink),
  headlineSmall: TextStyle(fontFamily: _josefin, fontWeight: FontWeight.w600, fontSize: 22, color: _ink),
  titleLarge: TextStyle(fontFamily: _josefin, fontWeight: FontWeight.w600, fontSize: 18, color: _ink),
  titleMedium: TextStyle(fontFamily: _josefin, fontWeight: FontWeight.w600, fontSize: 15, color: _ink),
  bodyLarge: TextStyle(fontFamily: _inter, fontWeight: FontWeight.w400, fontSize: 15, color: _ink),
  bodyMedium: TextStyle(fontFamily: _inter, fontWeight: FontWeight.w400, fontSize: 13, color: Brand.fogGrey),
  bodySmall: TextStyle(fontFamily: _inter, fontWeight: FontWeight.w400, fontSize: 12, color: Brand.fogGrey),
  labelLarge: TextStyle(fontFamily: _inter, fontWeight: FontWeight.w700, fontSize: 12, letterSpacing: 1.0, color: Brand.fogGrey),
);
