#include "TupleSampler.h"

#include <cstdint>
#include <vector>

namespace wisp {

namespace {

// FNV-1a 32-bit. Tiny, fast, inlinable. Good enough for "spread MACs across
// a small integer set"; we are NOT depending on it for security.
uint32_t fnv1a(const uint8_t* bytes, size_t n) {
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < n; ++i) {
    h ^= static_cast<uint32_t>(bytes[i]);
    h *= 0x01000193u;
  }
  return h;
}

// Hash a MAC with a 32-bit salt XORed in before hashing. The salt lets us
// derive uncorrelated outputs (one for idxA, one for idxB, one for the
// swap bit) from a single MAC.
//
// Two-stage mix:
//   1. FNV-1a over the salt-XORed bytes — spreads salt across the state.
//   2. Stafford variant-13 finalizer — FNV-1a alone has poor low-bit
//      avalanche for inputs that differ only in their low bytes (which is
//      the production case: the 22 lamps' MACs differ mostly in byte 5).
//      Without the finalizer, bit 0 of the output (used by the swap path)
//      and (h % n) for small n both bias hard, defeating the per-lamp
//      randomization. The finalizer is two multiplies + three xor-shifts —
//      cheaper than re-rolling, decisive on avalanche.
uint32_t hashMac(const uint8_t mac[6], uint32_t salt) {
  uint8_t buf[6];
  buf[0] = mac[0] ^ static_cast<uint8_t>(salt & 0xFF);
  buf[1] = mac[1] ^ static_cast<uint8_t>((salt >> 8) & 0xFF);
  buf[2] = mac[2] ^ static_cast<uint8_t>((salt >> 16) & 0xFF);
  buf[3] = mac[3] ^ static_cast<uint8_t>((salt >> 24) & 0xFF);
  buf[4] = mac[4] ^ static_cast<uint8_t>(salt & 0xFF);
  buf[5] = mac[5] ^ static_cast<uint8_t>((salt >> 16) & 0xFF);
  uint32_t h = fnv1a(buf, 6);
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

bool nearlyEqualColor(const RGBW& a, const RGBW& b) {
  // ~8/255 per channel — about the smallest step the lamp eye can perceive
  // on the WS2812 family. If adjacent authored stops are this close, treat
  // them as one so we don't waste a "discrete" pick on a near-duplicate.
  constexpr int kDeltaThreshold = 8;
  auto chDelta = [](uint8_t x, uint8_t y) {
    return std::abs(static_cast<int>(x) - static_cast<int>(y));
  };
  return chDelta(a.r, b.r) < kDeltaThreshold &&
         chDelta(a.g, b.g) < kDeltaThreshold &&
         chDelta(a.b, b.b) < kDeltaThreshold &&
         chDelta(a.w, b.w) < kDeltaThreshold;
}

// Dedupe adjacent near-identical stops. Stops are gradient control points;
// colors between them are interpolated when sampling.
std::vector<RGBW> dedupe(const std::vector<RGBW>& in) {
  std::vector<RGBW> out;
  out.reserve(in.size());
  for (const auto& c : in) {
    if (out.empty() || !nearlyEqualColor(out.back(), c)) {
      out.push_back(c);
    }
  }
  return out;
}

// Linear-interpolate two 0-255 channels at fixed-point fraction frac in
// [0, 2^32). All-unsigned so the Dart port matches byte for byte.
uint8_t lerp8(uint8_t a, uint8_t b, uint32_t frac) {
  const uint64_t inv = 0x100000000ULL - static_cast<uint64_t>(frac);
  return static_cast<uint8_t>(
      ((static_cast<uint64_t>(a) * inv +
        static_cast<uint64_t>(b) * static_cast<uint64_t>(frac)) >> 32));
}

// Sample the palette as a continuous gradient at fixed-point position pos in
// [0, 2^32): stops are control points, colors between them are interpolated.
RGBW sampleGradientAt(const std::vector<RGBW>& stops, uint32_t pos) {
  const uint32_t n = static_cast<uint32_t>(stops.size());
  if (n == 1) return stops[0];
  const uint64_t scaled = static_cast<uint64_t>(pos) * (n - 1);
  const uint32_t i = static_cast<uint32_t>(scaled >> 32);
  const uint32_t frac = static_cast<uint32_t>(scaled & 0xFFFFFFFFu);
  if (i >= n - 1) return stops[n - 1];
  RGBW out;
  out.r = lerp8(stops[i].r, stops[i + 1].r, frac);
  out.g = lerp8(stops[i].g, stops[i + 1].g, frac);
  out.b = lerp8(stops[i].b, stops[i + 1].b, frac);
  out.w = lerp8(stops[i].w, stops[i + 1].w, frac);
  return out;
}

}  // namespace

ColorTuple sampleTupleForMac(const CurrentPalette& palette,
                             const uint8_t mac[6],
                             uint32_t shuffleSeed) {
  ColorTuple out;
  if (!mac) return out;

  const auto& raw = palette.colors();
  if (raw.empty()) return out;

  auto stops = dedupe(raw);
  if (stops.empty()) return out;

  constexpr uint32_t kGolden   = 0x9E3779B9u;  // posB salt (decorrelates from posA)
  constexpr uint32_t kSwapSalt = 0xCAFEBABEu;  // independent third hash space
  constexpr uint32_t kMinGap   = 0x40000000u;  // 0.25 of the gradient span

  uint32_t posA = hashMac(mac, 0u       ^ shuffleSeed);
  uint32_t posB = hashMac(mac, kGolden  ^ shuffleSeed);

  // Keep the two positions >= kMinGap apart so base and shade are visually
  // distinct. Deterministic per MAC.
  const uint32_t d = posA > posB ? posA - posB : posB - posA;
  if (d < kMinGap) {
    posB = (posA <= 0xFFFFFFFFu - kMinGap) ? posA + kMinGap : posA - kMinGap;
  }

  const bool swap = (hashMac(mac, kSwapSalt ^ shuffleSeed) & 1u) != 0u;
  const RGBW base  = sampleGradientAt(stops, swap ? posB : posA);
  const RGBW shade = sampleGradientAt(stops, swap ? posA : posB);

  out.r[0] = base.r;  out.g[0] = base.g;  out.b[0] = base.b;  out.w[0] = base.w;
  out.r[1] = shade.r; out.g[1] = shade.g; out.b[1] = shade.b; out.w[1] = shade.w;
  return out;
}

}  // namespace wisp
