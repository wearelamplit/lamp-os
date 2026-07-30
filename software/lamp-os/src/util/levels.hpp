#pragma once

#include <cstdint>

#include "color.hpp"

namespace lamp {
uint8_t calculateBrightnessLevel(uint8_t value, uint8_t percentage);

// Scale an 8-bit value by an 8-bit fraction where 255 == 1.0. Rounds so
// fract 255 maps v to v exactly (identity); any fract < 255 only scales down.
inline uint8_t scale8(uint8_t v, uint8_t fract) {
  return static_cast<uint8_t>((static_cast<uint16_t>(v) * fract + 255) >> 8);
}

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
