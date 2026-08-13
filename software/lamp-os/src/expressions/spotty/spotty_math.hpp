#pragma once
#include <cstdint>

#include "util/easing.hpp"

namespace lamp {

// Overshoot/Spring easings leave applyEasing's [0,1] output range; clamps
// the eased fraction to a valid blend percent.
inline uint32_t clampPct(float f) {
  if (f <= 0.0f) return 0u;
  if (f >= 1.0f) return 100u;
  return static_cast<uint32_t>(f * 100.0f);
}

// Fade-in / hold / fade-out blend envelope in equal thirds. Returns the
// blend percent (0..100) for a spot `age` into a `life`-long cycle (both in
// the same unit, e.g. milliseconds); ages at or past `life` return 0. The
// easing curve shapes each fade ramp; Linear reproduces the plain ramp.
// Header-only as a native-test seam.
inline uint32_t spotBlendPercent(uint32_t age, uint32_t life,
                                 Easing easing = Easing::Linear) {
  const uint32_t third = life / 3;
  if (third == 0) return 100;
  if (age < third) {
    const float t = static_cast<float>(age) / static_cast<float>(third);
    return clampPct(applyEasing(easing, t));
  }
  if (age < 2 * third) return 100;
  const uint32_t outStart = 2 * third;
  const uint32_t outLen = (life > outStart) ? (life - outStart) : 1;
  const uint32_t elapsed = age - outStart;
  if (elapsed >= outLen) return 0;
  const float t = static_cast<float>(elapsed) / static_cast<float>(outLen);
  return clampPct(1.0f - applyEasing(easing, t));
}

struct SpotLifeBounds {
  uint32_t lo;
  uint32_t hi;
};

// Per-spot lifetime [min, max] (ms) at each end of spotSpeed. The GENTLE end
// (spotSpeed 1) is a calm mid band; the DREAMY end (spotSpeed 10) is a slower,
// wider band that drifts for tens of seconds. Tune per bench feel.
inline constexpr uint32_t kSpotGentleLoMs = 1800;
inline constexpr uint32_t kSpotGentleHiMs = 8000;
inline constexpr uint32_t kSpotDreamyLoMs = 8000;
inline constexpr uint32_t kSpotDreamyHiMs = 45000;

// lo and hi interpolate independently between the gentle (spotSpeed 1) and
// dreamy (spotSpeed 10) ends. Header-only as a native-test seam.
inline SpotLifeBounds spotLifeBounds(uint16_t spotSpeed) {
  uint32_t s = spotSpeed;
  if (s < 1u) s = 1u;
  if (s > 10u) s = 10u;
  uint32_t lo = kSpotGentleLoMs + (kSpotDreamyLoMs - kSpotGentleLoMs) * (s - 1u) / 9u;
  uint32_t hi = kSpotGentleHiMs + (kSpotDreamyHiMs - kSpotGentleHiMs) * (s - 1u) / 9u;
  if (hi <= lo) hi = lo + 1u;
  return {lo, hi};
}

}  // namespace lamp
