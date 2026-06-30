// Dart port of wisp/src/TupleSampler.cpp — local prediction of the two
// colors the wisp would paint onto a lamp with the given MAC, given the
// wisp's current palette.
//
// Used by the wisp config screen's "Painted lamps" section to show a
// per-lamp color preview without requiring a firmware-side per-lamp
// roster broadcast.
//
// iOS returns a UUID (not a bdAddr), so those lamps get no preview.

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

/// The lamp's mesh (ESP-NOW / WiFi-STA) MAC, derived from its lamp-reported
/// bdAddr. ESP32 makes the BLE MAC the STA base + 2, so the mesh MAC is the
/// BLE MAC minus 2 (full 48-bit, with borrow). This is the MAC the wisp keys
/// its per-lamp colour pick and claim roster on, so use it (not the raw BLE id)
/// to match or predict against wisp data.
List<int>? meshMacFromBdAddr(String bdAddr) {
  final out = parseMacFromBleId(bdAddr);
  if (out == null) return null;
  var borrow = 2;
  for (var i = 5; i >= 0 && borrow > 0; i--) {
    final v = out[i] - borrow;
    out[i] = v < 0 ? v + 256 : v;
    borrow = v < 0 ? 1 : 0;
  }
  return out;
}

int _lerp8(int a, int b, int frac) {
  final inv = 0x100000000 - frac;
  return (a * inv + b * frac) >> 32;
}

LampColor _sampleGradientAt(List<LampColor> stops, int pos) {
  final n = stops.length;
  if (n == 1) return stops[0];
  final scaled = pos * (n - 1);
  final i = scaled >> 32;
  final frac = scaled & 0xFFFFFFFF;
  if (i >= n - 1) return stops[n - 1];
  return LampColor(
    r: _lerp8(stops[i].r, stops[i + 1].r, frac),
    g: _lerp8(stops[i].g, stops[i + 1].g, frac),
    b: _lerp8(stops[i].b, stops[i + 1].b, frac),
    w: _lerp8(stops[i].w, stops[i + 1].w, frac),
  );
}

/// Run the same per-MAC sampling the wisp runs. Returns null when the
/// palette is empty (no authored colors to pick from).
///
/// [shuffleSeed] defaults to 0 (matches the firmware default). Pass the
/// wisp's current `shuffleSeed` from [WispStatus] so the preview stays in
/// lock-step with what the wisp actually broadcasts.
TuplePrediction? predictTuple({
  required List<int> mac,
  required List<LampColor> palette,
  int shuffleSeed = 0,
}) {
  if (mac.length != 6) return null;
  if (palette.isEmpty) return null;
  final stops = _dedupe(palette);
  if (stops.isEmpty) return null;

  const int kGolden   = 0x9E3779B9;
  const int kSwapSalt = 0xCAFEBABE;
  const int kMinGap   = 0x40000000;

  int posA = _hashMac(mac, 0       ^ shuffleSeed);
  int posB = _hashMac(mac, kGolden ^ shuffleSeed);
  final d = posA > posB ? posA - posB : posB - posA;
  if (d < kMinGap) {
    posB = (posA <= 0xFFFFFFFF - kMinGap) ? posA + kMinGap : posA - kMinGap;
  }
  final swap = (_hashMac(mac, kSwapSalt ^ shuffleSeed) & 1) != 0;
  final base  = _sampleGradientAt(stops, swap ? posB : posA);
  final shade = _sampleGradientAt(stops, swap ? posA : posB);
  return TuplePrediction(base: base, shade: shade);
}
