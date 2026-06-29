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

// Dedupe adjacent near-identical stops. No padding/interpolation — discrete
// picking means each surface gets one of the authored colors verbatim.
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

  constexpr uint32_t kGolden   = 0x9E3779B9u;  // idxB salt (decorrelates from idxA)
  constexpr uint32_t kSwapSalt = 0xCAFEBABEu;  // independent third hash space

  const uint32_t n = static_cast<uint32_t>(stops.size());
  uint32_t idxA = hashMac(mac, 0u          ^ shuffleSeed) % n;
  uint32_t idxB = hashMac(mac, kGolden     ^ shuffleSeed) % n;

  // If there's more than one distinct color and the hashes collided, nudge
  // idxB so each surface gets a different authored color. Deterministic per
  // MAC because (idxB + 1) % n is purely a function of n and idxB.
  if (n >= 2 && idxA == idxB) {
    idxB = (idxB + 1) % n;
  }

  // Per-MAC swap of base/shade assignment. Without this, surface[0] always
  // got the idxA color and surface[1] always got idxB — visually, the fleet
  // pivoted around whichever direction the hash distributions happened to
  // bias, producing patterns like "all blue tops, all red bottoms" on a
  // 2-color palette. Splitting ~50/50 across the fleet breaks that.
  const bool swap = (hashMac(mac, kSwapSalt ^ shuffleSeed) & 1u) != 0u;
  const RGBW& base  = swap ? stops[idxB] : stops[idxA];
  const RGBW& shade = swap ? stops[idxA] : stops[idxB];

  out.r[0] = base.r;  out.g[0] = base.g;  out.b[0] = base.b;  out.w[0] = base.w;
  out.r[1] = shade.r; out.g[1] = shade.g; out.b[1] = shade.b; out.w[1] = shade.w;
  return out;
}

}  // namespace wisp
