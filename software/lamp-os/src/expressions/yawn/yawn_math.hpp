#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "util/color.hpp"
#include "util/easing.hpp"

namespace lamp {

// One yawn's timeline. Levels are multiples of the scene's brightness.
struct YawnShape {
  uint32_t durationMs = 0;
  uint32_t inhaleMs = 0;
  uint32_t holdMs = 0;
  uint32_t recoverMs = 0;
  float lift = 0.0f;   // inhale peak above the scene
  float depth = 1.0f;  // the bottom, as a fraction of the scene
};

// Share of durationMs the inhale, hold and recover may take together. Past it
// they shrink in proportion, so the dim always has time to travel from the
// inhale peak to the bottom without a step.
inline constexpr float kYawnMaxFixedShare = 0.8f;

// Scene brightness multiplier `elapsedMs` into a yawn: exactly 1 at 0 and from
// durationMs on, with no step anywhere between.
inline float yawnLevel(const YawnShape& s, uint32_t elapsedMs) {
  if (elapsedMs >= s.durationMs) return 1.0f;
  const float duration = static_cast<float>(s.durationMs);
  const float fixed = static_cast<float>(s.inhaleMs) + s.holdMs + s.recoverMs;
  const float budget = duration * kYawnMaxFixedShare;
  const float k = fixed > budget ? budget / fixed : 1.0f;
  const float inhale = s.inhaleMs * k;
  const float hold = s.holdMs * k;
  const float recover = s.recoverMs * k;
  const float dim = duration - inhale - hold - recover;
  const float peak = inhale > 0.0f ? 1.0f + s.lift : 1.0f;
  auto ease = [](float from, float to, float t) {
    return from + (to - from) * applyEasing(Easing::Smooth, t);
  };

  float t = static_cast<float>(elapsedMs);
  if (t < inhale) return ease(1.0f, peak, t / inhale);
  t -= inhale;
  if (t < dim) return ease(peak, s.depth, t / dim);
  t -= dim;
  if (t < hold) return s.depth;
  t -= hold;
  return recover > 0.0f ? ease(s.depth, 1.0f, t / recover) : 1.0f;
}

// How far into the warm shift `level` sits: 0 at or above the scene, 1 at the
// bottom.
inline float yawnWarmWeight(float level, float depth) {
  if (level >= 1.0f || depth >= 1.0f) return 0.0f;
  return std::clamp((1.0f - level) / (1.0f - depth), 0.0f, 1.0f);
}

// Per-channel gains in Q8 (256 leaves a channel unchanged).
struct YawnGains {
  uint16_t r, g, b, w;
};

// Channel cuts at full warmth. Too high reads as a hue change, not a glow.
inline constexpr float kYawnWarmGreenCut = 0.25f;
inline constexpr float kYawnWarmBlueCut = 0.6f;

// Gains for a scene scaled to `level` and shifted warm by `warm` in [0,1].
// Level 1 with no warmth is exactly 256 on every channel.
inline YawnGains yawnGains(float level, float warm) {
  auto q8 = [](float f) {
    return static_cast<uint16_t>(std::lround(std::max(0.0f, f) * 256.0f));
  };
  return {q8(level), q8(level * (1.0f - kYawnWarmGreenCut * warm)),
          q8(level * (1.0f - kYawnWarmBlueCut * warm)), q8(level)};
}

// `c` under `g`, saturating at 255. A gain of 256 returns the channel exactly.
inline Color yawnPixel(const Color& c, const YawnGains& g) {
  auto ch = [](uint8_t v, uint16_t gain) {
    return static_cast<uint8_t>(
        std::min<uint32_t>(255u, (static_cast<uint32_t>(v) * gain + 128u) >> 8));
  };
  return Color(ch(c.r, g.r), ch(c.g, g.g), ch(c.b, g.b), ch(c.w, g.w));
}

}  // namespace lamp
