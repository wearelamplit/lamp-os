// Dart port of wisp/src/TupleSampler.cpp — local prediction of the two
// colors the wisp would paint onto a lamp with the given MAC, given the
// wisp's current palette.
//
// Used by the wisp config screen's "Painted lamps" section to show a
// per-lamp color preview without requiring a firmware-side per-lamp
// roster broadcast.
//
// Accuracy caveat: on Android, the BLE device id IS the lamp's MAC, but
// it's the BLE MAC — the ESP-NOW MAC differs by one byte (the ESP32 derives
// the WiFi-STA / ESP-NOW MAC by incrementing the BLE base). So this preview
// gives the right "shape" — varied per-lamp colors picked from the
// authored palette, with ~50/50 base/shade swap distribution — but the
// exact colors will not byte-match what the wisp actually paints. The UI
// notes this so the operator isn't surprised when their physical lamp
// shows different shades than the app preview.

import 'dart:typed_data';

import '../../control/domain/lamp_color.dart';

class TuplePrediction {
  const TuplePrediction({required this.base, required this.shade});
  final LampColor base;
  final LampColor shade;
}

int _fnv1a(Uint8List bytes) {
  // Use ints; Dart on a 64-bit VM holds these in native int (mod 2^64),
  // so we mask back to 32 bits after each multiply to mirror the C++
  // uint32_t behaviour exactly.
  int h = 0x811C9DC5;
  for (int i = 0; i < bytes.length; ++i) {
    h ^= bytes[i];
    h = (h * 0x01000193) & 0xFFFFFFFF;
  }
  return h;
}

int _hashMac(List<int> mac, int salt) {
  final buf = Uint8List(6);
  buf[0] = mac[0] ^ (salt & 0xFF);
  buf[1] = mac[1] ^ ((salt >> 8) & 0xFF);
  buf[2] = mac[2] ^ ((salt >> 16) & 0xFF);
  buf[3] = mac[3] ^ ((salt >> 24) & 0xFF);
  buf[4] = mac[4] ^ (salt & 0xFF);
  buf[5] = mac[5] ^ ((salt >> 16) & 0xFF);
  int h = _fnv1a(buf);
  h ^= h >> 16;
  h = (h * 0x7feb352d) & 0xFFFFFFFF;
  h ^= h >> 15;
  h = (h * 0x846ca68b) & 0xFFFFFFFF;
  h ^= h >> 16;
  return h;
}

bool _nearlyEqual(LampColor a, LampColor b) {
  const int delta = 8;
  return (a.r - b.r).abs() < delta &&
      (a.g - b.g).abs() < delta &&
      (a.b - b.b).abs() < delta &&
      (a.w - b.w).abs() < delta;
}

List<LampColor> _dedupe(List<LampColor> in_) {
  final out = <LampColor>[];
  for (final c in in_) {
    if (out.isEmpty || !_nearlyEqual(out.last, c)) out.add(c);
  }
  return out;
}

/// Parse a BLE id string into a 6-byte MAC. Returns null when the id
/// doesn't look like a colon-hex MAC (e.g. iOS opaque UUIDs).
List<int>? parseMacFromBleId(String id) {
  final parts = id.split(':');
  if (parts.length != 6) return null;
  final out = <int>[];
  for (final p in parts) {
    final v = int.tryParse(p, radix: 16);
    if (v == null || v < 0 || v > 255) return null;
    out.add(v);
  }
  return out;
}

/// Run the same per-MAC sampling the wisp runs. Returns null when the
/// palette is empty (no authored colors to pick from).
TuplePrediction? predictTuple({
  required List<int> mac,
  required List<LampColor> palette,
}) {
  if (mac.length != 6) return null;
  if (palette.isEmpty) return null;
  final stops = _dedupe(palette);
  if (stops.isEmpty) return null;

  const int kGolden   = 0x9E3779B9;
  const int kSwapSalt = 0xCAFEBABE;
  final int n = stops.length;
  int idxA = _hashMac(mac, 0)        % n;
  int idxB = _hashMac(mac, kGolden)  % n;
  if (n >= 2 && idxA == idxB) idxB = (idxB + 1) % n;
  final swap = (_hashMac(mac, kSwapSalt) & 1) != 0;
  final base  = swap ? stops[idxB] : stops[idxA];
  final shade = swap ? stops[idxA] : stops[idxB];
  return TuplePrediction(base: base, shade: shade);
}
