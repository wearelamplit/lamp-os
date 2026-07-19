// StatusRing holds pure helpers rendering the wisp's 30-pixel ring as a live
// indicator of source/palette state. Strip control lives in main.cpp; only
// the palette→pixels gradient math is here so it stays host-testable.
// No heap, no Arduino headers.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

// Fixed ring length. Kept here so tests and production share one constant.
inline constexpr size_t kStatusRingPixelCount = 30;

// Upper bound for configurable pixel count (NVS / app-settable).
inline constexpr size_t kMaxRingPixels = 100;

// Warm-white fallback when the ring has no palette (Off, or empty Manual/
// Aurora). Pulled warmer than a neutral warm-white because at brightness 40
// (~16% scale) the WS2812 compresses the R:G:B ratio and washes a paler tint
// out; these scale to roughly (40, 24, 8) on the LEDs, a clear candle-amber.
inline constexpr uint8_t kWarmWhiteR = 255;
inline constexpr uint8_t kWarmWhiteG = 150;
inline constexpr uint8_t kWarmWhiteB = 50;

// Render numStops RGB stops into outRgb (3 bytes/pixel, length pixelCount*3)
// with linear interpolation. Returns false and leaves outRgb untouched when
// numStops == 0 (caller fills warm-white). stopsRgb is flat numStops*3 RGB.
inline bool computeRingGradient(const uint8_t* stopsRgb,
                                size_t numStops,
                                uint8_t* outRgb,
                                size_t pixelCount) {
  if (numStops == 0 || pixelCount == 0 || stopsRgb == nullptr ||
      outRgb == nullptr) {
    return false;
  }

  if (numStops == 1) {
    const uint8_t r = stopsRgb[0];
    const uint8_t g = stopsRgb[1];
    const uint8_t b = stopsRgb[2];
    for (size_t i = 0; i < pixelCount; ++i) {
      outRgb[i * 3 + 0] = r;
      outRgb[i * 3 + 1] = g;
      outRgb[i * 3 + 2] = b;
    }
    return true;
  }

  // numStops >= 2: map each pixel to a fractional stop position and lerp in
  // Q16.16 fixed-point (deterministic across host/MCU). Q16 product fits in uint32_t.
  const uint32_t denom = static_cast<uint32_t>(pixelCount - 1);
  const uint32_t span  = static_cast<uint32_t>(numStops - 1);
  for (size_t i = 0; i < pixelCount; ++i) {
    // t_q16 = (i * span * 65536) / denom, fractional palette-index in Q16.16.
    const uint32_t tQ16 = (static_cast<uint32_t>(i) * span * 65536u) / denom;
    const uint32_t lo = tQ16 >> 16;        // floor stop index
    uint32_t hi = lo + 1;                  // ceil stop index
    if (hi > span) hi = span;              // clamp the trailing edge
    const uint32_t frac = tQ16 & 0xFFFFu;  // 0..65535 weight on hi

    const uint8_t* a = stopsRgb + lo * 3;
    const uint8_t* b = stopsRgb + hi * 3;
    // (a * (65536 - frac) + b * frac) >> 16, rounded by adding 32768.
    const uint32_t invFrac = 65536u - frac;
    outRgb[i * 3 + 0] = static_cast<uint8_t>(
        (a[0] * invFrac + b[0] * frac + 32768u) >> 16);
    outRgb[i * 3 + 1] = static_cast<uint8_t>(
        (a[1] * invFrac + b[1] * frac + 32768u) >> 16);
    outRgb[i * 3 + 2] = static_cast<uint8_t>(
        (a[2] * invFrac + b[2] * frac + 32768u) >> 16);
  }
  return true;
}

// Fill `outRgb` (length pixelCount*3) with the warm-white fallback color.
inline void fillRingWarmWhite(uint8_t* outRgb, size_t pixelCount) {
  if (outRgb == nullptr) return;
  for (size_t i = 0; i < pixelCount; ++i) {
    outRgb[i * 3 + 0] = kWarmWhiteR;
    outRgb[i * 3 + 1] = kWarmWhiteG;
    outRgb[i * 3 + 2] = kWarmWhiteB;
  }
}

// Fold an Aurora RGBW sample to RGB for the NEO_GRB ring (no W channel). The
// W contribution biases warm (most lands on R, some on G, almost none on B)
// so a mostly-white palette still reads warm rather than washed-out. Each
// channel clamps to 255.
inline void rgbwToRgbWarmBias(uint8_t inR, uint8_t inG, uint8_t inB,
                              uint8_t inW,
                              uint8_t& outR, uint8_t& outG, uint8_t& outB) {
  // 0.7 * W → R, 0.4 * W → G, scaled with integer /10.
  const uint16_t addR = (static_cast<uint16_t>(inW) * 7u) / 10u;
  const uint16_t addG = (static_cast<uint16_t>(inW) * 4u) / 10u;
  const uint16_t r = static_cast<uint16_t>(inR) + addR;
  const uint16_t g = static_cast<uint16_t>(inG) + addG;
  outR = r > 255 ? 255 : static_cast<uint8_t>(r);
  outG = g > 255 ? 255 : static_cast<uint8_t>(g);
  outB = inB;
}

}  // namespace wisp
