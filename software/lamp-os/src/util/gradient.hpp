#pragma once

#include <cstdint>
#include <vector>

#include "color.hpp"
#include "fade.hpp"

namespace lamp {
/**
 * make a smooth gradient from one color to another
 * @param [in] inColorStart - the start color
 * @param [in] inColorEnd - the end color
 * @param [in] inSteps - the number of pixels the gradients should span
 * @return a vector of colors for the steps specified
 */
std::vector<Color> calculateGradient(Color inColorStart, Color inColorEnd,
                                     uint8_t inSteps);

/**
 * given a list of colors, evenly fill a pixel buffer with a smooth
 * gradient
 * @param [in] inNumberPixels the total Neopixel count to spread the gradient
 * @param [in] inColorSteps the colors to fade between, cut past 8
 * @return gradiented colors for the total strip length specified
 */
std::vector<Color> buildGradientWithStops(uint8_t inNumberPixels,
                                          std::vector<Color> inColorStops);

/**
 * how many of `stops` the gradient builders use, at most 8, counted in
 * 8 bits as they count them
 */
uint8_t gradientStopCount(const std::vector<Color>& stops);

/**
 * the closed form of buildGradientWithStops, with one more segment from the last
 * stop back to the first, so pixel px - 1 leads into pixel 0 as any stop
 * boundary does. Fewer than 2 stops build what buildGradientWithStops builds.
 */
std::vector<Color> buildGradientLoop(uint8_t px, const std::vector<Color>& stops);
}  // namespace lamp
