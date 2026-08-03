#pragma once
#include <cstdint>

#include "../../util/easing.hpp"

namespace lamp { namespace lioness {

// Compositor render rate; AnimatedBehavior advances one frame per render
// tick (core/animated_behavior.hpp). Bench-tunable if that rate ever changes.
constexpr uint32_t kFrameRateHz = 60;

// Breaths in the greeting train; drop to 3 for an even calmer arrival.
constexpr uint32_t kPulses = 4;
// One breath (inhale+exhale), in ms; tune this, not kFramesPerPulse
// directly. 2.4s reads as a slow, gentle swell rather than an excited pulse.
constexpr uint32_t kPulseMs        = 2400;
constexpr uint32_t kFramesPerPulse = kPulseMs * kFrameRateHz / 1000;
// Hand-off fade after the last pulse; scales with kPulseMs so the ease-out
// doesn't snap relative to the slow breath.
constexpr uint32_t kGreetEaseMs     = 1200;
constexpr uint32_t kGreetEaseFrames = kGreetEaseMs * kFrameRateHz / 1000;
constexpr uint32_t kGreetFrames = kPulses * kFramesPerPulse + kGreetEaseFrames;

constexpr uint8_t kPulsePeak = 255;
// Trough floor as a fraction of peak; the higher the floor the gentler the
// swing. 0.67 breathes between "mostly on" and full, never dimming much.
// Bench-tunable.
constexpr uint8_t kPulseFloor = static_cast<uint8_t>(kPulsePeak * 0.67f);

namespace detail {
// Raised-cosine over one full pulse period: 0 at p=0, 1 at the midpoint, back
// to 0 at p=framesPerPulse. No corners, unlike a triangle ramp.
inline float raisedCosinePulse(uint32_t p, uint32_t framesPerPulse) {
  constexpr float kTwoPi = 6.283185307179586f;
  const float t = kTwoPi * static_cast<float>(p) / static_cast<float>(framesPerPulse);
  return 0.5f - 0.5f * std::cos(t);
}
}  // namespace detail

// Breathing brightness (0..255) for the greeting pulse train: kPulses smooth
// floor->peak->floor cycles, then an ease-out hand-off to ambient. The tail
// starts exactly where the last pulse leaves off and decays monotonically to
// 0, so there's no re-brighten flash at the pulse->ease boundary.
inline uint8_t pulseEnvelope(uint32_t frame) {
  const uint32_t pulseWindow = kPulses * kFramesPerPulse;
  if (frame < pulseWindow) {
    const uint32_t p = frame % kFramesPerPulse;
    const float raised = detail::raisedCosinePulse(p, kFramesPerPulse);
    return static_cast<uint8_t>(kPulseFloor + raised * (kPulsePeak - kPulseFloor));
  }
  if (frame < kGreetFrames) {
    const float trough = detail::raisedCosinePulse(kFramesPerPulse - 1, kFramesPerPulse);
    const float handoff = kPulseFloor + trough * (kPulsePeak - kPulseFloor);
    const uint32_t e = frame - pulseWindow;                // 0..kGreetEaseFrames
    const float t = static_cast<float>(e) / static_cast<float>(kGreetEaseFrames);
    const float eased = applyEasing(Easing::Smooth, t);
    return static_cast<uint8_t>(handoff * (1.0f - eased));
  }
  return 0;
}

}}  // namespace lioness, namespace lamp
