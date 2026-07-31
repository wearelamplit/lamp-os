#pragma once

#include <cstdint>
#include <vector>

#include "util/color.hpp"
#include "util/fade.hpp"

namespace lamp {

inline float clampUnit(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// restLevel/amp are the heat band; stepMs the per-pixel flicker responsiveness;
// windAmp/gust* the global gust (windAmp 0 = still). sparkChance is the
// per-roll probability a heat target instead lands in the hot [sparkLo,sparkHi]
// ember band (0 = never). Starting calibration, tuned on the bench.
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
};

inline FireStyle fireStyle(uint32_t value) {
  static constexpr FireStyle table[] = {
    // Twinkle: near-dark base, sparse sparks that rise and fade slowly, no wind. Cool colors read as stars.
    {0.00f, 0.02f, 900, 0.00f,    0,    0, 0.030f, 0.60f, 1.00f},
    // Coals: a dim ember bed, several embers breathing in and out in place, no wind.
    {0.12f, 0.05f, 1600, 0.00f,    0,    0, 0.045f, 0.55f, 0.85f},
    // Candle: a quick, flickery flame reacting to fast air jitter.
    {0.36f, 0.16f, 220, 0.14f, 1500, 3500, 0.00f, 0.00f, 0.00f},
    // Campfire: lively, brighter, regular gusts.
    {0.50f, 0.28f, 180, 0.18f, 3000, 7000, 0.00f, 0.00f, 0.00f},
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

// Point-sample an ordered cool->hot gradient at p in [0,1]. No allocation.
inline Color sampleGradient(const std::vector<Color>& stops, float p) {
  if (stops.empty()) return Color{};
  if (stops.size() == 1) return stops.front();
  p = clampUnit(p);
  const float scaled = p * static_cast<float>(stops.size() - 1);
  size_t i = static_cast<size_t>(scaled);
  if (i >= stops.size() - 1) return stops.back();
  const float frac = scaled - static_cast<float>(i);
  return mixColorLinear(stops[i], stops[i + 1],
                        static_cast<uint32_t>(frac * 262144.0f));
}

// Warm ramp for an empty palette so a fresh shimmer looks like fire. Static so
// draw() never allocates.
inline const std::vector<Color>& defaultFireRamp() {
  static const std::vector<Color> ramp = {
    Color{60, 0, 0, 0},
    Color{255, 60, 0, 0},
    Color{255, 160, 20, 0},
  };
  return ramp;
}

inline constexpr float kShimmerMinBright = 0.15f;

}  // namespace lamp
