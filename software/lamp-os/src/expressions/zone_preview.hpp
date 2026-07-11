#pragma once

#include <map>
#include <string>
#include <vector>

#include "expressions/primitives.hpp"
#include "util/color.hpp"

namespace lamp {

// Per-pixel buffer for the editor's live Zone preview: pixels in
// [posMin,posMax] lit to `color`, every other pixel off. Reuses
// Zone::fromParameters so the lit span matches exactly what a Zone-driven
// expression would select (clamp to [0,pixelCount-1], swap if reversed).
inline std::vector<Color> buildZonePreviewBuffer(uint16_t pixelCount,
                                                 uint32_t posMin,
                                                 uint32_t posMax, Color color) {
  std::vector<Color> buf(pixelCount, Color());
  if (pixelCount == 0) return buf;
  const std::map<std::string, uint32_t> p = {{"posMin", posMin},
                                             {"posMax", posMax}};
  const Zone z = Zone::fromParameters(p, pixelCount);
  for (uint16_t i = z.posMin; i <= z.posMax && i < pixelCount; ++i) {
    buf[i] = color;
  }
  return buf;
}

}  // namespace lamp
