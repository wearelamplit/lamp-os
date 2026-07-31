#pragma once

#include <algorithm>
#include <cstdint>

#include "util/color.hpp"

namespace lamp {

inline float clampUnit(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// restLevel/amp are the heat band; stepMs the per-pixel flicker responsiveness;
// windAmp/gust* the global smooth gust (windAmp 0 = still). sparkChance is the
// per-roll probability a heat target instead lands in the hot [sparkLo,sparkHi]
// ember band (0 = never). flutterAmp is the amplitude of a fast per-frame global
// brightness random-walk layered on top of the smooth movement (0 = none).
// warmthSwing/whiteHot shape how heat modulates the lamp's own colour (see
// warmthModulate): warmthSwing is the hue slide toward yellow at peak / maroon
// at floor (0 = pure brightness), whiteHot the desaturation toward white at the
// hot tip (0 = stays saturated). Starting calibration, tuned on the bench.
struct FireStyle {
  float restLevel;
  float amp;
  uint32_t stepMs;
  float windAmp;
  uint32_t gustLoMs;
  uint32_t gustHiMs;
  float sparkChance;
  float sparkLo;
  float sparkHi;
  float flutterAmp;
  float warmthSwing;
  float whiteHot;
};

inline FireStyle fireStyle(uint32_t value) {
  static constexpr FireStyle table[] = {
    // Twinkle: near-dark base, sparse sparks that rise and fade slowly, no wind.
    {0.00f, 0.02f, 900, 0.00f,    0,    0, 0.030f, 0.60f, 1.00f, 0.00f, 0.15f, 0.00f},
    // Coals: a dim ember bed, several embers breathing in and out in place, no wind.
    {0.12f, 0.05f, 1600, 0.00f,    0,    0, 0.045f, 0.55f, 0.85f, 0.00f, 0.30f, 0.00f},
    // Candle: a slow convective sway plus a fast turbulent brightness flutter.
    {0.36f, 0.16f, 220, 0.24f, 1500, 3500, 0.00f, 0.00f, 0.00f, 0.15f, 0.55f, 0.15f},
    // Campfire: lively, brighter, regular gusts with a subtle flutter.
    {0.50f, 0.28f, 180, 0.18f, 3000, 7000, 0.00f, 0.00f, 0.00f, 0.04f, 0.80f, 0.40f},
  };
  if (value > 3) value = 3;
  return table[value];
}

template <class Rng>
inline float rollHeatTarget(const FireStyle& s, Rng& rng) {
  if (s.sparkChance > 0.0f &&
      rng.range(0, 1000) / 1000.0f < s.sparkChance) {
    const float t = rng.range(0, 1000) / 1000.0f;
    return clampUnit(s.sparkLo + (s.sparkHi - s.sparkLo) * t);
  }
  const float lo = clampUnit(s.restLevel - s.amp);
  const float hi = clampUnit(s.restLevel + s.amp);
  const float t = rng.range(0, 1000) / 1000.0f;
  return lo + (hi - lo) * t;
}

// Global gust target in [-windAmp, +windAmp]. windAmp 0 (still air) yields 0.
template <class Rng>
inline float rollWindTarget(const FireStyle& s, Rng& rng) {
  return (rng.range(0, 2000) / 1000.0f - 1.0f) * s.windAmp;
}

template <class Rng>
inline uint32_t nextGustAt(uint32_t nowMs, const FireStyle& s, Rng& rng) {
  return nowMs + rng.range(s.gustLoMs, s.gustHiMs);
}

inline constexpr float kFlutterStepFrac = 0.5f;

// One step of a bounded fast random-walk in [-flutterAmp, +flutterAmp]. Each call
// nudges by up to +/- kFlutterStepFrac*amp and clamps. amp 0 yields 0.
template <class Rng>
inline float advanceFlutter(float flutter, float amp, Rng& rng) {
  if (amp <= 0.0f) return 0.0f;
  const float step = amp * kFlutterStepFrac;
  const float delta = (rng.range(0, 2000) / 1000.0f - 1.0f) * step;
  const float v = flutter + delta;
  return v < -amp ? -amp : (v > amp ? amp : v);
}

inline float approachHeat(float heat, float target, uint32_t deltaMs,
                          uint32_t stepMs) {
  if (stepMs == 0) return target;
  float k = static_cast<float>(deltaMs) / static_cast<float>(stepMs);
  if (k > 1.0f) k = 1.0f;
  return heat + (target - heat) * k;
}

inline float heatBrightness(float heat, float minBright) {
  return minBright + (1.0f - minBright) * clampUnit(heat);
}

inline constexpr float kShimmerColdFloor = 0.20f;   // channel scale at coldest
inline constexpr float kShimmerHotGain = 0.35f;     // brightness lift at hottest
inline constexpr float kShimmerColdBlueKill = 1.3f; // blue attenuates past green when cold

// Modulate the anchor colour along a dim->warm ramp driven by heat. neutral is
// the style rest heat: heat == neutral returns the anchor unchanged. Above it
// the anchor brightens and (warmthSwing) slides green toward red for a yellower
// tip, with whiteHot engaging W at the very top; below it the anchor darkens and
// green/blue fall faster than red for a maroon floor. Channel-space only, no HSV
// round-trip; a black anchor stays black.
inline Color warmthModulate(Color anchor, float heat, float neutral,
                            float warmthSwing, float whiteHot) {
  float w;
  if (heat >= neutral)
    w = (neutral < 1.0f) ? (heat - neutral) / (1.0f - neutral) : 0.0f;
  else
    w = (neutral > 0.0f) ? (heat - neutral) / neutral : 0.0f;
  w = w < -1.0f ? -1.0f : (w > 1.0f ? 1.0f : w);

  const float bscale = (w >= 0.0f) ? 1.0f + w * kShimmerHotGain
                                   : 1.0f + w * (1.0f - kShimmerColdFloor);
  float r = anchor.r * bscale;
  float g = anchor.g * bscale;
  float b = anchor.b * bscale;
  float wc = anchor.w * bscale;

  if (warmthSwing > 0.0f) {
    const float hot = warmthSwing * (w > 0.0f ? w : 0.0f);
    const float cold = warmthSwing * (w < 0.0f ? -w : 0.0f);
    g += (r - g) * hot;
    g *= 1.0f - cold;
    b *= 1.0f - cold * kShimmerColdBlueKill;
  }

  if (whiteHot > 0.0f && w > 0.0f) {
    const float lum = std::max(r, std::max(g, b)) / 255.0f;
    wc += (255.0f - wc) * whiteHot * w * clampUnit(lum);
  }

  auto clamp8 = [](float v) -> uint8_t {
    return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : static_cast<uint8_t>(v + 0.5f));
  };
  return Color{clamp8(r), clamp8(g), clamp8(b), clamp8(wc)};
}

inline constexpr float kShimmerMinBright = 0.15f;

}  // namespace lamp
