#include "levels.hpp"

#include <cstdint>

#include "color.hpp"

namespace lamp {
uint8_t calculateBrightnessLevel(uint8_t value, uint8_t percentage) {
  // Clamp percentage to [0,100] so a bad input can't overflow the
  // downstream (value * p) / 100 past 255.
  uint8_t p = percentage > 100 ? 100 : percentage;

  return (value * p) / 100;
};

Color setColorBrightness(Color inColor, uint8_t percentage) {
  if (percentage >= 100) {
    return inColor;
  }

  return Color(
      calculateBrightnessLevel(inColor.r, percentage),
      calculateBrightnessLevel(inColor.g, percentage),
      calculateBrightnessLevel(inColor.b, percentage),
      calculateBrightnessLevel(inColor.w, percentage));
};
}  // namespace lamp