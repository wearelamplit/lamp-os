import '../../../features/control/domain/lamp_color.dart';

typedef WispClaimsParse = ({
  Set<String> macs,
  Map<String, ({LampColor base, LampColor shade})> colors,
});

/// Parses the binary CHAR_WISP_CLAIMS payload.
///
/// Wire format: `[count:1][mac:6]*count`, count ≤ 32, max 193 bytes.
/// Returns a set of uppercase colon-separated MAC strings
/// (e.g. "AA:BB:CC:DD:EE:FF") — the same format as WispStatus.wispMac
/// and the inverse of parseMacFromBleId. Empty/short/malformed blobs
/// return an empty set without throwing.
Set<String> parseClaimedMacs(List<int> blob) => parseWispClaims(blob).macs;

/// Parses the full CHAR_WISP_CLAIMS payload including the optional
/// trailing color section.
///
/// Wire format: `[count:1][mac:6*count][colorPair:6*count]?`
/// Colors present only when `blob.length >= 1 + count*12`. An all-zero
/// colorPair is the sentinel for "no live color" and is omitted from
/// the returned map. Malformed blobs return empty fields without throwing.
WispClaimsParse parseWispClaims(List<int> blob) {
  const empty = (macs: <String>{}, colors: <String, ({LampColor base, LampColor shade})>{});
  if (blob.isEmpty) return empty;
  final count = blob[0] & 0xFF;
  if (count == 0) return empty;
  if (blob.length < 1 + count * 6) return empty;

  final macs = <String>{};
  for (var i = 0; i < count; i++) {
    macs.add(_macAt(blob, 1 + i * 6));
  }

  final colors = <String, ({LampColor base, LampColor shade})>{};
  final colorOffset = 1 + count * 6;
  if (blob.length >= colorOffset + count * 6) {
    for (var i = 0; i < count; i++) {
      final o = colorOffset + i * 6;
      final r0 = blob[o] & 0xFF;
      final g0 = blob[o + 1] & 0xFF;
      final b0 = blob[o + 2] & 0xFF;
      final r1 = blob[o + 3] & 0xFF;
      final g1 = blob[o + 4] & 0xFF;
      final b1 = blob[o + 5] & 0xFF;
      // Sentinel: both triples are zero → no live color for this mac.
      if (r0 == 0 && g0 == 0 && b0 == 0 && r1 == 0 && g1 == 0 && b1 == 0) {
        continue;
      }
      final mac = _macAt(blob, 1 + i * 6);
      colors[mac] = (
        base: LampColor(r: r0, g: g0, b: b0, w: 0),
        shade: LampColor(r: r1, g: g1, b: b1, w: 0),
      );
    }
  }

  return (
    macs: Set.unmodifiable(macs),
    colors: Map.unmodifiable(colors),
  );
}

String _macAt(List<int> bytes, int offset) => [
      for (var i = offset; i < offset + 6; i++)
        (bytes[i] & 0xFF).toRadixString(16).padLeft(2, '0').toUpperCase(),
    ].join(':');
