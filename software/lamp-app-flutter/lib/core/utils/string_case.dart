extension TitleCase on String {
  /// Title-cases each word: first letter upper, rest lower, whitespace kept.
  /// Lamps store names canonically lowercase; this is the display form.
  /// `jacko` -> `Jacko`, `living room` -> `Living Room`, `` -> ``.
  String toTitleCase() => toLowerCase()
      .replaceAllMapped(RegExp(r'\b\w'), (m) => m[0]!.toUpperCase());
}
