/// Parses the binary CHAR_WISP_CLAIMS payload.
///
/// Wire format: `[count:1][mac:6]*count`, count ≤ 32, max 193 bytes.
/// Returns a set of uppercase colon-separated MAC strings
/// (e.g. "AA:BB:CC:DD:EE:FF") — the same format as WispStatus.wispMac
/// and the inverse of parseMacFromBleId. Empty/short/malformed blobs
/// return an empty set without throwing.
Set<String> parseClaimedMacs(List<int> blob) {
  if (blob.isEmpty) return const {};
  final count = blob[0] & 0xFF;
  if (count == 0) return const {};
  if (blob.length < 1 + count * 6) return const {};
  final out = <String>{};
  for (var i = 0; i < count; i++) {
    out.add(_macAt(blob, 1 + i * 6));
  }
  return Set.unmodifiable(out);
}

String _macAt(List<int> bytes, int offset) => [
      for (var i = offset; i < offset + 6; i++)
        (bytes[i] & 0xFF).toRadixString(16).padLeft(2, '0').toUpperCase(),
    ].join(':');
