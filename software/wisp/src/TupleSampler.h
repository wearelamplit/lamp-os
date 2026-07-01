// TupleSampler — pure function (palette, mac) → two RGBW colors.
// FNV-1a + Stafford-13 finalizer; finalizer is required for good avalanche
// when fleet MACs differ only in the low byte. Host-portable: no Arduino.

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

// Pure function: same palette + mac + shuffleSeed always returns the same
// tuple. Edge cases: empty palette → zeros; single color → both surfaces.
ColorTuple sampleTupleForMac(const CurrentPalette& palette,
                             const uint8_t mac[6],
                             uint32_t shuffleSeed = 0);

}  // namespace wisp
