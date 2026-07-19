/// Lamp personality. Wire values match firmware's `SocialMode` enum in
/// `software/lamp-os/src/config/config_types.hpp`: `int` 0..2, carried
/// by the Dart enum via `.wire`.
enum SocialMode {
  introvert(0, 'Introvert'),
  ambivert(1, 'Ambivert'),
  extrovert(2, 'Extrovert');

  const SocialMode(this.wire, this.label);

  /// Numeric value used on the BLE wire and in NVS JSON.
  final int wire;

  /// Display label for the personality selector segments.
  final String label;

  static SocialMode fromWire(int? value) {
    switch (value) {
      case 0:
        return SocialMode.introvert;
      case 2:
        return SocialMode.extrovert;
      case 1:
      default:
        return SocialMode.ambivert;
    }
  }
}
