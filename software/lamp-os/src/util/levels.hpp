#pragma once

#include <cstdint>

#include "color.hpp"

namespace lamp {
/**
 * @brief dim a pixel value by a brightness level expressed as a percentage
 */
uint8_t calculateBrightnessLevel(uint8_t value, uint8_t percentage);

/**
 * @brief modify a color's total brightness by percentage
 */
Color setColorBrightness(Color inColor, uint8_t percentage);
}  // namespace lamp
