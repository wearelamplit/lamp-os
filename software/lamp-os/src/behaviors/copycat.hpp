#pragma once

#include <cstdint>

#include <lampos/clock.hpp>

#include "behaviors/roll_cooldown.hpp"
#include "config/config_types.hpp"

namespace lamp {

// Mirror-chance grid, all bench-tunable feel numbers.
// rate% = kMirrorBasePct[disposition] * kMirrorModeFactorX10[mode] / 10,
// clamped 0..100. Only Fond(4)/Smitten(5) ever mirror.
inline constexpr uint8_t kMirrorBasePct[6] = {0, 0, 0, 0, 20, 50};
// Introvert ×0.5, Ambivert ×1.0, Extrovert ×1.5 (fixed-point ×10).
inline constexpr uint8_t kMirrorModeFactorX10[3] = {5, 10, 15};
// Introvert-only floor between mirrors so an introvert doesn't chatter.
inline constexpr uint32_t kIntrovertMirrorCooldownMs = 600000;
// Replay lands 0.4-0.8 s after the announce: always just-after, never lockstep.
inline constexpr uint32_t kMirrorDelayFloorMs = 400;
inline constexpr uint32_t kMirrorJitterMs = 400;

// Resolved mirror chance (0..100) for a disposition + mode. Pure.
inline uint8_t mirrorRatePct(uint8_t disposition, SocialMode mode) {
  if (disposition >= 6) return 0;
  const uint8_t modeIdx = static_cast<uint8_t>(mode);
  if (modeIdx >= 3) return 0;
  const uint32_t rate = static_cast<uint32_t>(kMirrorBasePct[disposition]) *
                        kMirrorModeFactorX10[modeIdx] / 10;
  return rate > 100 ? 100 : static_cast<uint8_t>(rate);
}

// Copycat feel numbers, live-tunable over serial on a debug build.
struct ContagionTuning {
  // One roll per source peer per window, whether or not it fired.
  uint32_t copyRollCooldownMs = 180000;
  // A copy of hop k (1 = a copy of an origin) rolls while k <= chainMaxHops and
  // announces while k < chainMaxHops. 1 never announces a copy.
  uint8_t chainMaxHops = 3;
  // Each hop past the first rolls at this percent of the hop before.
  uint8_t chainHopPct = 50;
  // Odds rise this percent per meeting with the source, up to 10 meetings.
  uint8_t familiarBoostPct = 0;
};

inline constexpr uint8_t kMaxChainHops = 8;
inline constexpr uint32_t kMaxCopyRollCooldownMs = 3600000;
inline constexpr uint8_t kMaxFamiliarBoostPct = 3;
inline constexpr uint8_t kFamiliarBoostMeets = 10;

// Chance (0..100) of copying a frame `frameHops` from its origin, from the
// mirror chance `basePct` and `meets` with its sender. 0 when the copy would
// pass chainMaxHops. Pure.
inline uint8_t copyChancePct(uint8_t basePct, uint8_t frameHops, uint8_t meets,
                             const ContagionTuning& t) {
  if (frameHops >= t.chainMaxHops) return 0;
  const uint32_t counted = meets < kFamiliarBoostMeets ? meets : kFamiliarBoostMeets;
  uint32_t pct = static_cast<uint32_t>(basePct) * (100 + counted * t.familiarBoostPct) / 100;
  for (uint8_t h = 0; h < frameHops; ++h) pct = pct * t.chainHopPct / 100;
  return pct > 100 ? 100 : static_cast<uint8_t>(pct);
}

// Sets ContagionTuning field `key` (its member name) to `value`. False, with
// `t` unchanged, for an unknown key or a value out of range.
bool setContagionTuning(ContagionTuning& t, const char* key, uint32_t value);

// The introvert cooldown's last mirror. retire() forgets one older than the
// cooldown, so it cannot gate a mirror again one millis() wrap later; the
// owner calls it at least once per lampos::kRetiredAgeMs.
struct MirrorCooldown {
  bool ever = false;
  uint32_t lastMs = 0;

  void mirrored(uint32_t nowMs) {
    ever = true;
    lastMs = nowMs;
  }
  void retire(uint32_t nowMs) {
    if (ever && lampos::msSince(nowMs, lastMs) >= kIntrovertMirrorCooldownMs) ever = false;
  }
};

// Pure mirror decision at chance `ratePct` (0..100). `roll` is a 1..100
// chance draw, `jitterRoll` a 0..kMirrorJitterMs draw.
// `everMirrored`/`lastMirrorMs` carry the introvert-cooldown state. Returns
// true and sets `outFireAt` when a replay should be scheduled; false to skip.
inline bool mirrorDecision(uint8_t ratePct, SocialMode mode, uint8_t roll,
                           uint32_t jitterRoll, uint32_t nowMs,
                           bool everMirrored, uint32_t lastMirrorMs,
                           uint32_t& outFireAt) {
  if (ratePct == 0) return false;
  if (roll > ratePct) return false;
  if (mode == SocialMode::Introvert && everMirrored &&
      nowMs - lastMirrorMs < kIntrovertMirrorCooldownMs) {
    return false;
  }
  const uint32_t jitter =
      jitterRoll > kMirrorJitterMs ? kMirrorJitterMs : jitterRoll;
  outFireAt = nowMs + kMirrorDelayFloorMs + jitter;
  return true;
}

enum class CopyOutcome : uint8_t { NoChance, Cooldown, Skip, Fire };

struct CopyDecision {
  CopyOutcome outcome = CopyOutcome::NoChance;
  // The chance rolled at, 0..100.
  uint8_t pct = 0;
  // The copy's own hop count, one past the frame's.
  uint8_t hops = 0;
  // When a Fire's replay is due.
  uint32_t fireAt = 0;
};

// Whether to copy one peer's announced expression, holding the per-peer roll
// cooldown and the introvert floor. Fixed RAM whatever the crowd.
class Copycat {
 public:
  // A frame `frameHops` from its origin, from `mac`. `chanceRoll` is a 1..100
  // draw, `jitterRoll` a 0..kMirrorJitterMs draw.
  CopyDecision decide(const uint8_t mac[6], uint8_t disposition, SocialMode mode,
                      uint8_t frameHops, uint32_t nowMs, uint8_t chanceRoll,
                      uint32_t jitterRoll);
  // A Fire's replay was booked; starts the introvert floor.
  void booked(uint32_t nowMs) { cooldown_.mirrored(nowMs); }
  // True when a copy carrying `hops` announces itself for the next lamp.
  bool announces(uint8_t hops) const { return hops < tuning.chainMaxHops; }
  // Call at least once per lampos::kRetiredAgeMs.
  void retire(uint32_t nowMs);

  ContagionTuning tuning;

 private:
  RollCooldownTable rolls_;
  MirrorCooldown cooldown_;
};

}  // namespace lamp
