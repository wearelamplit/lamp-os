import 'package:flutter/material.dart';

abstract class BrandColors {
  static const amberGold = Color(0xFFE1A44A);
  static const ashGrey = Color(0xFF555555);
  static const auroraBlue = Color(0xFF446C9C);
  static const cloudGrey = Color(0xFFE0E0E0);
  static const fogGrey = Color(0xFFCCCCCC);
  static const glowPink = Color(0xFFEFA3C8);
  static const lampWhite = Color(0xFFFDFDFD);
  static const lumenGreen = Color(0xFF8DCDA6);
  static const midnightBlack = Color(0xFF0D0D0D);
  static const slateGrey = Color(0xFF888888);
  static const softGrey = Color(0xFFF5F5F5);

  // Ported from the prior Vue/Capacitor app (mobile-app-port branch,
  // software/lamp-app/src/assets/base.css). Adds tokens the new Flutter
  // surface was missing.

  /// Warm-white channel reference colour — used by `LampColorSwatch`'s
  /// screen-blend overlay and as the warm-white slider's thumb tint.
  static const warmWhite = Color(0xFFFABB3E);

  /// Hover/pressed state for [auroraBlue]. Use on interactive surfaces
  /// that should remain in the brand-blue family on press.
  static const auroraBlueHover = Color(0xFF5A7BA8);

  /// Brand error red. Replaces the slightly peachy Material default we
  /// used to ship in ColorScheme.error.
  static const error = Color(0xFFF87171);

  /// Bright header accent (currently unused in Flutter; kept available
  /// for the eventual port of the Vue page headers).
  static const headerYellow = Color(0xFFFFFA77);

  /// Lime-green header accent (same provenance as headerYellow).
  static const headerLime = Color(0xFFDDFF77);

  /// Default outline border for text-style inputs. Ported from the Vue
  /// app's `--color-background-mute` (`Text.vue:103`, `Number.vue:113`).
  static const inputBorder = Color(0xFF2A2A2A);

  /// Warm-beige "Hello my name is:" preamble colour from
  /// `CritterNameplate.vue:48`.
  static const nameplateGrey = Color(0xFFB1AA92);
}
