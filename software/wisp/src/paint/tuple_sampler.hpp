// TupleSampler is a pure function (palette, mac) → two RGBW colors.
// FNV-1a + Stafford-13 finalizer; finalizer is required for good avalanche
// when fleet MACs differ only in the low byte. Host-portable: no Arduino.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "paint/current_palette.hpp"

namespace wisp {

// Two RGBW colors, indexed [0]/[1]. The wisp takes no opinion on surface
// mapping; receivers decide via the MSG_OVERRIDE_COLORS surface byte.
struct ColorTuple {
  uint8_t r[2] = {0, 0};
  uint8_t g[2] = {0, 0};
  uint8_t b[2] = {0, 0};
  uint8_t w[2] = {0, 0};

  // Color `i` packed as R,G,B,W.
  void rgbw(size_t i, uint8_t out[4]) const {
    out[0] = r[i];
    out[1] = g[i];
    out[2] = b[i];
    out[3] = w[i];
  }
};

// Pure function: same palette + mac + shuffleSeed + style always returns
// the same tuple. Edge cases: empty palette → zeros; single color → both
// surfaces. The app's predictTuple mirrors it byte for byte.
ColorTuple sampleTupleForMac(const std::vector<RGBW>& colors,
                             const uint8_t mac[6],
                             uint32_t shuffleSeed = 0,
                             PaletteStyle style = PaletteStyle::Gradient);

// Sample base/shade at two explicit positions (full uint32 span). Empty
// palette → zeros. sampleTupleForMac and the random drift path share this
// tail; feed it esp_random() for a fresh random pair. The sampling rule per
// style is in docs/dev/networking.md (Wisp paints a lamp's base + shade).
ColorTuple sampleTupleAtPositions(const std::vector<RGBW>& colors,
                                  uint32_t posA, uint32_t posB,
                                  PaletteStyle style = PaletteStyle::Gradient);

}  // namespace wisp
