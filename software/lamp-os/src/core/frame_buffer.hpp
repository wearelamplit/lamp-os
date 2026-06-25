#pragma once

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include <vector>

#include "util/color.hpp"

namespace lamp {
/**
 * @brief the frame buffer holds a copy of the current scene's pixel
 * colors for all layered draw operations. The lamp will only draw
 * when the bitmap buffer or brightness is changed
 */
class FrameBuffer {
 public:
  std::vector<Color> defaultColors;
  std::vector<Color> previousBuffer;
  uint8_t previousBrightness = 0;
  uint8_t pixelCount = 0;
  Adafruit_NeoPixel* driver = nullptr;
  std::vector<Color> buffer;

  // Rate-limit floor for the LAMP_DEBUG-gated flush log so per-frame
  // changes during a fade don't drown the serial. Per-instance so base
  // and shade each get their own throttled stream — set to 0 to log the
  // next change, otherwise gated to once per ~500 ms.
  uint32_t lastFlushLogMs_ = 0;

  FrameBuffer();

  /**
   * @brief Setup initializer
   * @param [in] inDefaultColors The user's default lamp colors {Color, ..., n} n <= 50
   * @param [in] inPixelCount the number of neopixels in use
   * @param [in] inDriver the NeoPixel instance to use
   */
  void begin(std::vector<Color> inDefaultColors, uint8_t inPixelCount, Adafruit_NeoPixel* inDriver);

  /**
   * @brief send values from buffer to the LED driver
   */
  void flush();
};
}  // namespace lamp
