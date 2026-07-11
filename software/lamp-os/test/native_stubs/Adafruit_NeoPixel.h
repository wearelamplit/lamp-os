#pragma once
// Minimal fake for native FrameBuffer tests. Provides enough surface area for
// frame_buffer.cpp to compile and for tests to observe driver call sequences.
#include <cstdint>
#include <vector>

class Adafruit_NeoPixel {
 public:
  struct PixelCall {
    uint16_t n;
    uint32_t c;
  };

  bool canShowResult = true;
  uint8_t brightness = 0;
  std::vector<PixelCall> pixelCalls;
  int showCount = 0;
  int beginCount = 0;
  int fillCount = 0;

  bool begin() { ++beginCount; return true; }
  void fill(uint32_t = 0, uint16_t = 0, uint16_t = 0) { ++fillCount; }
  void show() { ++showCount; }
  void setPixelColor(uint16_t n, uint32_t c) { pixelCalls.push_back({n, c}); }
  bool canShow() { return canShowResult; }
  uint8_t getBrightness() const { return brightness; }

  void reset() {
    pixelCalls.clear();
    showCount = 0;
  }
};
