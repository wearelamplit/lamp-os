#pragma once

#include <Adafruit_NeoPixel.h>

#include <cstdint>
#include <vector>

#include "util/color.hpp"

namespace lamp {

// Geometry-only strip segment. Palettes live in Config.
struct StripSegment {
  Adafruit_NeoPixel* driver;
  const char* name;
  uint16_t offset;     // start index in the logical buffer
  uint16_t pixelCount;
};

class FrameBuffer {
 public:
  std::vector<Color> defaultColors;
  std::vector<Color> previousBuffer;
  uint8_t previousBrightness = 0;
  uint8_t pixelCount = 0;
  std::vector<Color> buffer;
  std::vector<StripSegment> segments;

  FrameBuffer();

  // Primary form: sizes buffer to Σ seg.pixelCount; initializes every segment driver.
  // Σ pixelCount must fit in uint8_t (enforced upstream in validateHwConfig/config_codec).
  void begin(std::vector<Color> inDefaultColors, std::vector<StripSegment> inSegments);

  void begin(std::vector<Color> inDefaultColors, uint8_t inPixelCount, Adafruit_NeoPixel* inDriver);

  void flush();
};

}  // namespace lamp
