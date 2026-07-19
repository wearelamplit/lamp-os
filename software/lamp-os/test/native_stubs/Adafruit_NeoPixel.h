#pragma once
// Minimal fake for native FrameBuffer tests. Provides enough surface area for
// frame_buffer.cpp to compile and for tests to observe driver call sequences.
#include <cmath>
#include <cstdint>
#include <vector>

// NEO_* pixel format constants (values match the real Adafruit library).
#ifndef NEO_GRB
#define NEO_GRB  0x52
#define NEO_GRBW 0x62
#define NEO_BGR  0x31
#define NEO_KHZ800 0x0000
#endif

class Adafruit_NeoPixel {
 public:
  struct PixelCall {
    uint16_t n;
    uint32_t c;
  };

  static uint8_t gamma8(uint8_t v) {
    return (uint8_t)(pow(v / 255.0, 2.6) * 255 + 0.5);
  }

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
