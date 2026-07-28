#pragma once

#include <cmath>
#include <cstdint>

namespace lamp {

enum class Easing : uint8_t {
  Linear = 0, Smooth = 1, Float = 2, Settle = 3, Swell = 4,
  SnapIn = 5,  SnapOut = 6,  SnapInOut = 7,               // expo
  OvershootIn = 8, OvershootOut = 9, OvershootInOut = 10, // back
  SpringIn = 11, SpringOut = 12, SpringInOut = 13,        // elastic
  BounceIn = 14, BounceOut = 15, BounceInOut = 16,        // bounce
  Random = 17,
};

// Fraction of Float's travel held flat at each end so the lava wave lingers at
// 0 and 1. Bench-tunable.
inline constexpr float kFloatDwell = 0.12f;

namespace detail {
inline float outExpo(float t)  { return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
inline float outBack(float t)  { constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
                                 const float u = t - 1.0f; return 1.0f + c3 * u * u * u + c1 * u * u; }
inline float outElastic(float t) { if (t <= 0.0f || t >= 1.0f) return t;
                                 constexpr float c4 = 2.0f * 3.14159265358979323846f / 3.0f;
                                 return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f; }
inline float outBounce(float t) { constexpr float n1 = 7.5625f, d1 = 2.75f;
  if (t < 1.0f / d1)      return n1 * t * t;
  if (t < 2.0f / d1)    { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
  if (t < 2.5f / d1)    { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
  t -= 2.625f / d1;       return n1 * t * t + 0.984375f; }

inline float easeIn(float (*fOut)(float), float t)  { return 1.0f - fOut(1.0f - t); }
inline float easeInOut(float (*fOut)(float), float t) {
  return t < 0.5f ? (1.0f - fOut(1.0f - 2.0f * t)) * 0.5f
                  : (1.0f + fOut(2.0f * t - 1.0f)) * 0.5f;
}
}  // namespace detail

// Maps progress t in [0,1] through curve e; input clamps to [0,1]. Overshoot,
// Spring, and Bounce curves intentionally leave their output outside [0,1]
// mid-travel; callers casting the result to an unsigned/narrow type must
// clamp first (see spotBlendPercent, breathing's intensity cast).
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
    case Easing::SnapIn:        return detail::easeIn(detail::outExpo, t);
    case Easing::SnapOut:       return detail::outExpo(t);
    case Easing::SnapInOut:     return detail::easeInOut(detail::outExpo, t);
    case Easing::OvershootIn:   return detail::easeIn(detail::outBack, t);
    case Easing::OvershootOut:  return detail::outBack(t);
    case Easing::OvershootInOut:return detail::easeInOut(detail::outBack, t);
    case Easing::SpringIn:      return detail::easeIn(detail::outElastic, t);
    case Easing::SpringOut:     return detail::outElastic(t);
    case Easing::SpringInOut:   return detail::easeInOut(detail::outElastic, t);
    case Easing::BounceIn:      return detail::easeIn(detail::outBounce, t);
    case Easing::BounceOut:     return detail::outBounce(t);
    case Easing::BounceInOut:   return detail::easeInOut(detail::outBounce, t);
    case Easing::Random:        return t;  // resolved before render; never drawn directly
  }
  return t;
}

// Concrete curve for a "Random" motion, drawn once per fire. Excludes Random
// itself; range spans Linear..BounceInOut.
template <class Rng>
inline Easing randomEasing(Rng& rng) {
  return static_cast<Easing>(rng.range(0u, static_cast<uint32_t>(Easing::BounceInOut)));
}

// Maps an integer step within [0,dur] through curve e, returning the eased
// step in the same [0,dur] scale. Linear (or dur == 0) returns step unchanged,
// so a linear caller stays bit-identical. Overshoot/Spring/Bounce curves can
// push applyEasing outside [0,1]; clamped here so the result always stays in
// [0,dur] for callers that don't clamp themselves.
inline uint32_t easeStep(uint32_t step, uint32_t dur, Easing e) {
  if (e == Easing::Linear || dur == 0) return step;
  const float t = static_cast<float>(step) / static_cast<float>(dur);
  float f = applyEasing(e, t);
  if (f < 0.0f) f = 0.0f;
  if (f > 1.0f) f = 1.0f;
  return static_cast<uint32_t>(f * static_cast<float>(dur));
}

}  // namespace lamp
