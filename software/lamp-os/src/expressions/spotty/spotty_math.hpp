#pragma once
#include <cstdint>

#include "util/easing.hpp"

namespace lamp {

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
    return static_cast<uint32_t>(applyEasing(easing, t) * 100.0f);
  }
  if (age < 2 * third) return 100;
  const uint32_t outStart = 2 * third;
  const uint32_t outLen = (life > outStart) ? (life - outStart) : 1;
  const uint32_t elapsed = age - outStart;
  if (elapsed >= outLen) return 0;
  const float t = static_cast<float>(elapsed) / static_cast<float>(outLen);
  return static_cast<uint32_t>((1.0f - applyEasing(easing, t)) * 100.0f);
}

struct SpotLifeBounds {
  uint32_t lo;
  uint32_t hi;
};

// Per-spot lifetime [min, max] (ms) at each end of spotSpeed. The FIRE end is a
// wide, low band: quick flickers mixed with slower flames. The STARS end is a
// narrow, high band: slow, gentle, never fast. Tune per bench feel.
inline constexpr uint32_t kSpotFireLoMs  = 30;
inline constexpr uint32_t kSpotFireHiMs  = 2000;
inline constexpr uint32_t kSpotStarsLoMs = 4000;
inline constexpr uint32_t kSpotStarsHiMs = 15000;

// lo and hi interpolate independently between the fire (spotSpeed 1) and stars
// (spotSpeed 10) ends. Header-only as a native-test seam.
inline SpotLifeBounds spotLifeBounds(uint16_t spotSpeed) {
  uint32_t s = spotSpeed;
  if (s < 1u) s = 1u;
  if (s > 10u) s = 10u;
  uint32_t lo = kSpotFireLoMs + (kSpotStarsLoMs - kSpotFireLoMs) * (s - 1u) / 9u;
  uint32_t hi = kSpotFireHiMs + (kSpotStarsHiMs - kSpotFireHiMs) * (s - 1u) / 9u;
  if (hi <= lo) hi = lo + 1u;
  return {lo, hi};
}

}  // namespace lamp
