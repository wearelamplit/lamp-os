/// 4/8pt spacing scale + radius tokens. Replaces magic-number EdgeInsets.
abstract class AppSpace {
  static const double xs = 4;
  static const double sm = 8;
  static const double md = 12;
  static const double lg = 16;
  static const double xl = 24;
  static const double xxl = 32;
}

abstract class AppRadius {
  static const double card = 12;

  /// Corner radius for every color swatch (rounded square). Matches the
  /// Colors-page cards so all swatches rhyme.
  static const double swatch = 14; // deliberate dimension, not spacing
}
