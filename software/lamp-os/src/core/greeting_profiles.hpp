#pragma once

// The greeting-waveform table + the pure disposition×mode → profile mapping.
// Split from personality_engine so the mapping is a header-only native-test
// seam (like social_tier.hpp): pinnable without linking the Config/roster
// runtime. PersonalityEngine::greetingFor is the sole runtime consumer.

#include <cstdint>

#include "config/config_types.hpp"  // SocialMode
#include "core/social_tier.hpp"     // socialTier
#include "util/easing.hpp"          // Easing

namespace lamp {

// Sentinel for GreetingTuning::pulseBackCount meaning "fill the entire
// hold window with back-to-back cycles" instead of a fixed cycle count.
constexpr uint8_t kPulseCountContinuous = 0xFF;

// A peer's greeting waveform parameters. Returned by greetingFor() and
// consumed by SocialBehavior's draw()/playOnce().
struct GreetingTuning {
  uint32_t totalFrames     = 0;
  uint32_t easeInFrames    = 0;
  uint32_t holdFrames      = 0;
  uint32_t fadeOutFrames   = 0;
  uint8_t  pulseBackStrength = 0;
  uint8_t  pulseBackCount    = 0;
  // Frames per warm-breath cycle (dim-down + brighten-back). Faster =
  // more eager. Only meaningful when pulseBackStrength > 0.
  uint16_t breathCycleFrames = 120;
  // Snub waveform: the ease-in fades shade → peer color while dimming
  // brightness to pulseBackStrength depth (255 = black), holds dark, and
  // reverses on the way out. Warm greetings (snub=false) instead reach
  // full peer color and use pulseBackStrength/Count for in-hold pulses.
  bool     snub              = false;
  // Motion of the color ramps. draw() routes the ease-in/ease-out
  // POSITION through applyEasing(curve, t), so disposition reads in the
  // arrival/departure curve, not just the timing.
  Easing   easeInCurve  = Easing::Smooth;
  Easing   easeOutCurve = Easing::Smooth;
  // Motion of the in-hold breath (warm profiles). Float dwells at the top +
  // bottom of each breath (calm); Smooth breathes continuously (eager).
  Easing   breathCurve  = Easing::Float;
};

namespace greeting_profiles {

// Waveform profiles in compositor frames (~60 fps). Anchored on
// kProfileStandard (Ambivert greeting a Neutral peer) = ~2.8s in / 16s hold /
// 2.5s out, the neutral baseline. Every profile holds >= 5s so no greeting
// reads as a blink, every ramp is >= 2s so nothing snaps, and the ease-out
// lengthens with warmth for a reluctant goodbye. Disposition reads in the
// MOTION as much as the timing: warm greetings swell in and smooth back out;
// snubs hold a deep dim floor (pulseBackStrength) so a brush-off reads as a
// cold-shoulder, not a quick flash.
struct Profile {
  uint32_t total;
  uint32_t easeIn;
  uint32_t hold;
  uint32_t fadeOut;
  uint8_t  pulseBackStrength;
  uint8_t  pulseBackCount;
  uint16_t breathCycleFrames;
  bool     snub;
  Easing   easeInCurve;
  Easing   easeOutCurve;
  Easing   breathCurve;
};

// Warm-breath cycle lengths (frames at ~60 fps). Warmer disposition breathes
// faster; a 3 s floor keeps even the eagerest breath from reading as a
// flutter. Bench-tunable. Non-pulsing profiles carry the default; the field is
// inert when pulseBackStrength is 0.
inline constexpr uint16_t kDefaultBreathFrames  = 120;
inline constexpr uint16_t kWarmBreathFrames     = 300;  // ~5.0 s, slow + calm
inline constexpr uint16_t kEnthusedBreathFrames = 240;  // ~4.0 s
inline constexpr uint16_t kEffusiveBreathFrames = 180;  // ~3.0 s, eager

// 5s at ~60 fps; the shortest a greeting lingers. Bench-tunable.
inline constexpr uint32_t kMinHoldFrames = 300;

inline constexpr Profile kProfileMinimal   = {1080, 180,  780, 120, 0, 0, kDefaultBreathFrames, false,
                                              Easing::Smooth, Easing::Smooth, Easing::Float};
inline constexpr Profile kProfileGentle     = {1176, 156,  840, 180, 0, 0, kDefaultBreathFrames, false,
                                              Easing::Swell, Easing::Smooth, Easing::Float};
inline constexpr Profile kProfileStandard   = {1278, 168,  960, 150, 0, 0, kDefaultBreathFrames, false,
                                              Easing::Smooth, Easing::Smooth, Easing::Float};
// Warm pulse depths are a gentle glow-breath, not a flash. Bench-tunable.
inline constexpr uint8_t kWarmPulseDim     = 50;
inline constexpr uint8_t kEnthusedPulseDim = 65;
inline constexpr uint8_t kEffusivePulseDim = 80;

// Warm greetings breathe for the entire hold; depth + cycle speed carry the
// disposition (deeper + faster = more excited).
inline constexpr Profile kProfileWarm       = {1404, 144, 1020, 240, kWarmPulseDim,
                                              kPulseCountContinuous, kWarmBreathFrames, false,
                                              Easing::Swell, Easing::Smooth, Easing::Float};
inline constexpr Profile kProfileEnthused   = {1512, 132, 1080, 300, kEnthusedPulseDim,
                                              kPulseCountContinuous, kEnthusedBreathFrames, false,
                                              Easing::Swell, Easing::Smooth, Easing::Smooth};
inline constexpr Profile kProfileEffusive   = {1620, 120, 1140, 360, kEffusivePulseDim,
                                              kPulseCountContinuous, kEffusiveBreathFrames, false,
                                              Easing::Swell, Easing::Smooth, Easing::Smooth};
// Snub dim depth: 191 → ~25% brightness, 165 → ~35%. A real cold floor,
// never fully off. Bench-tunable.
inline constexpr uint8_t kFullSnubDim    = 191;
inline constexpr uint8_t kPartialSnubDim = 165;

inline constexpr Profile kProfileSnub        = {540, 120, kMinHoldFrames, 120, kFullSnubDim, 1, kDefaultBreathFrames, true,
                                              Easing::Smooth, Easing::Smooth, Easing::Float};
inline constexpr Profile kProfilePartialSnub = {570, 120, kMinHoldFrames, 150, kPartialSnubDim, 1, kDefaultBreathFrames, true,
                                              Easing::Smooth, Easing::Smooth, Easing::Float};

inline GreetingTuning toTuning(const Profile& p) {
  GreetingTuning t;
  t.totalFrames       = p.total;
  t.easeInFrames      = p.easeIn;
  t.holdFrames        = p.hold;
  t.fadeOutFrames     = p.fadeOut;
  t.pulseBackStrength = p.pulseBackStrength;
  t.pulseBackCount    = p.pulseBackCount;
  t.breathCycleFrames = p.breathCycleFrames;
  t.snub              = p.snub;
  t.easeInCurve       = p.easeInCurve;
  t.easeOutCurve      = p.easeOutCurve;
  t.breathCurve       = p.breathCurve;
  return t;
}

// Openness-ladder rung [0..7] → greeting profile, 1:1 with social_tier.hpp.
inline const Profile& profileForTier(uint8_t tier) {
  static constexpr const Profile* kByTier[8] = {
      &kProfileSnub,   &kProfilePartialSnub, &kProfileMinimal,  &kProfileStandard,
      &kProfileGentle, &kProfileWarm,        &kProfileEnthused, &kProfileEffusive};
  return *kByTier[tier];
}

inline const Profile& profileFor(SocialMode mode, uint8_t disposition) {
  return profileForTier(socialTier(disposition, mode));
}

}  // namespace greeting_profiles

// Greeting waveform for (mode, disposition): the single mapping the runtime
// and the profile-table tests both exercise.
inline GreetingTuning greetingTuningFor(SocialMode mode, uint8_t disposition) {
  return greeting_profiles::toTuning(greeting_profiles::profileFor(mode, disposition));
}

}  // namespace lamp
