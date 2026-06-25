/// Where the wisp gets its paint palette from. Mirrors the wisp-side
/// `WispSourceMode` enum in `software/wisp/src/WispConfig.h`:
///   off    — wisp does not broadcast paint; lamps run local behavior.
///   manual — wisp broadcasts paint from an operator-defined palette.
///   aurora — wisp follows its Aurora subscription (default; legacy
///            first-seen-wins behavior).
///
/// Wire-format on `wispStatus.source` is the lowercase enum name; the
/// wispOp setter accepts the same strings.
enum WispSourceMode {
  off,
  manual,
  aurora,
}

/// Decode a wire-format `source` string into the enum. Anything we don't
/// recognise (older wisp, future field, missing key) maps to [off] — the
/// safe default. The wisp emits paint frames only when source is Manual
/// or Aurora, so falling back to Off can never accidentally override the
/// lamps; the user has to explicitly opt in.
WispSourceMode parseWispSourceMode(String? raw) {
  switch (raw) {
    case 'off':
      return WispSourceMode.off;
    case 'manual':
      return WispSourceMode.manual;
    case 'aurora':
      return WispSourceMode.aurora;
    default:
      return WispSourceMode.off;
  }
}

/// Wire-format string for a source mode. Used in the `wispOp setSource`
/// JSON payload so the dispatcher on the wisp accepts it directly without
/// numeric coercion.
String wispSourceModeWire(WispSourceMode m) {
  switch (m) {
    case WispSourceMode.off:
      return 'off';
    case WispSourceMode.manual:
      return 'manual';
    case WispSourceMode.aurora:
      return 'aurora';
  }
}
