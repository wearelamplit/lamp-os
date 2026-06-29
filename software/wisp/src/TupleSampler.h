// TupleSampler — pure function (palette, mac) → two RGBW colors.
//
// Each peer gets two distinct authored palette stops at MAC-hashed indices
// (picks, never blends), with a per-MAC swap bit deciding base/shade order,
// so the same MAC + palette always yields the same pair and the fleet shows
// real endpoint colors rather than mid-tone blends. hash() is FNV-1a + a
// Stafford-13 finalizer (TupleSampler.cpp); the finalizer is what gives the
// swap bit + modulo-N indices good avalanche for fleet MACs that differ only
// in the low byte. Host-portable: no Arduino, no FreeRTOS.

#pragma once

#include <cstddef>
#include <cstdint>

#include "CurrentPalette.h"

namespace wisp {

// Two RGBW colors, indexed [0]/[1]. The wisp takes no opinion on surface
// mapping; receivers decide via the MSG_OVERRIDE_COLORS surface byte.
struct ColorTuple {
  uint8_t r[2] = {0, 0};
  uint8_t g[2] = {0, 0};
  uint8_t b[2] = {0, 0};
  uint8_t w[2] = {0, 0};
};

// Pure function. Determinism property: same `palette.colors()` snapshot +
// same `mac` always returns the same tuple. Edge cases:
//   - Empty palette → {0,0,0,0} for both colors.
//   - Single-color palette (post-dedupe) → that color for both surfaces.
//   - 2+ colors → discrete picks; each surface gets one of the authored
//     stops verbatim (no blending).
ColorTuple sampleTupleForMac(const CurrentPalette& palette,
                             const uint8_t mac[6]);

}  // namespace wisp
