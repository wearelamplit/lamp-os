#pragma once

#include <cstdint>

#include "color.hpp"

namespace lamp {
uint8_t calculateBrightnessLevel(uint8_t value, uint8_t percentage);

Color setColorBrightness(Color inColor, uint8_t percentage);

// Scale baseline by factor (0..1) and floor the result at floorPct, but never
// rise above baseline. A baseline already below the floor is left alone, and a
// baseline of 0 stays 0 (a dim never lights an off lamp).
uint8_t applyDimFactor(uint8_t baseline, float factor, uint8_t floorPct);

// The narrower of the variant hardware cap and the user's ceiling. The user
// value floors at 1 so a bad 0 can never blank the strip.
inline uint8_t effectiveCeiling(uint8_t variantMax, uint8_t userCeiling) {
  const uint8_t u = userCeiling < 1 ? 1 : userCeiling;
  return u < variantMax ? u : variantMax;
}
}  // namespace lamp
