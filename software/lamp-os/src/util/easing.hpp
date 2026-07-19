#pragma once

#include <cmath>
#include <cstdint>

namespace lamp {

enum class Easing : uint8_t { Linear = 0, Smooth = 1, Float = 2, Settle = 3, Swell = 4 };

// Fraction of Float's travel held flat at each end so the lava wave lingers at
// 0 and 1. Bench-tunable.
inline constexpr float kFloatDwell = 0.12f;

// Maps progress t in [0,1] through curve e; input and output clamp to [0,1].
inline float applyEasing(Easing e, float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;

  constexpr float pi = 3.14159265358979323846f;
  switch (e) {
    case Easing::Linear:
      return t;
    case Easing::Smooth:
      return t * t * (3.0f - 2.0f * t);
    case Easing::Float: {
      if (t <= kFloatDwell) return 0.0f;
      if (t >= 1.0f - kFloatDwell) return 1.0f;
      const float u = (t - kFloatDwell) / (1.0f - 2.0f * kFloatDwell);
      return 0.5f - 0.5f * std::cos(pi * u);
    }
    case Easing::Settle:
      return 1.0f - (1.0f - t) * (1.0f - t);
    case Easing::Swell:
      return t * t;
  }
  return t;
}

// Maps an integer step within [0,dur] through curve e, returning the eased
// step in the same [0,dur] scale. Linear (or dur == 0) returns step unchanged,
// so a linear caller stays bit-identical.
inline uint32_t easeStep(uint32_t step, uint32_t dur, Easing e) {
  if (e == Easing::Linear || dur == 0) return step;
  const float t = static_cast<float>(step) / static_cast<float>(dur);
  return static_cast<uint32_t>(applyEasing(e, t) * static_cast<float>(dur));
}

}  // namespace lamp
