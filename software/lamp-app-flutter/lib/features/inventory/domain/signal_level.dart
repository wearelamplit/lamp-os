/// Coarse closeness bucket derived from a lamp's scan RSSI.
enum SignalLevel { strong, medium, weak, unknown }

/// Buckets a scan RSSI (dBm, less-negative = closer) into a [SignalLevel].
/// A null RSSI — the lamp isn't currently seen by the scan — is
/// [SignalLevel.unknown], distinct from a weak-but-present signal.
SignalLevel signalLevelFor(int? rssi) {
  if (rssi == null) return SignalLevel.unknown;
  if (rssi >= -60) return SignalLevel.strong;
  if (rssi >= -75) return SignalLevel.medium;
  return SignalLevel.weak;
}

/// Green status-dot brightness for a lamp's RSSI: strong is full bright,
/// weaker signals dim. Unknown (not currently seen) defaults to medium.
double signalDotBrightnessFor(int? rssi) => switch (signalLevelFor(rssi)) {
      SignalLevel.strong => 1.0,
      SignalLevel.medium => 0.65,
      SignalLevel.weak => 0.4,
      SignalLevel.unknown => 0.65,
    };
