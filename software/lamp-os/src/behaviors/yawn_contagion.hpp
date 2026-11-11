#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "behaviors/copycat.hpp"
#include "behaviors/roll_cooldown.hpp"
#include "config/config_types.hpp"

namespace lamp {

// Yawn feel numbers, live-tunable over serial on a debug build.
struct YawnTuning {
  // One yawn-back roll per source peer per window, whether or not it fired.
  uint32_t yawnRollCooldownMs = 300000;
  // This lamp yawns at most once per floor, its own yawns and yawn-backs
  // alike, so it never yawns back at a wave it started.
  uint32_t yawnSelfFloorMs = 60000;
  // Yawn-back percent per disposition, Salty (1) to Smitten (5), before the
  // mode factor, familiarity, crowd and hop damping.
  std::array<uint8_t, 5> backPct = {0, 5, 15, 35, 60};
  // A tired introvert rolls this percent once per tiredYawnEveryMs.
  uint8_t tiredYawnPct = 35;
  uint32_t tiredYawnEveryMs = 90000;
};

inline constexpr uint32_t kMaxYawnWindowMs = 3600000;
inline constexpr uint32_t kMinTiredYawnEveryMs = 1000;

// Yawn-back chance (0..100) for a yawn `frameHops` from its origin: the
// disposition's backPct times the mirror mode factor, then copyChancePct's
// familiarity, crowd and hop damping. Pure.
uint8_t yawnBackPct(uint8_t disposition, SocialMode mode, uint8_t frameHops, uint8_t meets,
                    size_t nearby, const YawnTuning& y, const ContagionTuning& c);

// Sets YawnTuning field `key` to `value`: a member name, or yawn.salty /
// yawn.wary / yawn.neutral / yawn.fond / yawn.smitten for backPct. False, with
// `t` unchanged, for an unknown key or a value out of range.
bool setYawnTuning(YawnTuning& t, const char* key, uint32_t value);

enum class TiredRoll : uint8_t { NotDue, SelfFloor, Skip, Fire };

// Whether to yawn: a tired introvert's own roll, and a yawn-back at a peer's
// yawn. Fixed RAM whatever the crowd.
class YawnContagion {
 public:
  // Call every tick. While `tired`, rolls once per tiredYawnEveryMs, the first
  // a full interval after tiring. `chanceRoll` is a 1..100 draw; `yawnedRecently`
  // is whether this lamp yawned within tuning.yawnSelfFloorMs.
  TiredRoll tiredRoll(bool tired, uint32_t nowMs, uint8_t chanceRoll, bool yawnedRecently);

  // A peer's yawn `frameHops` from its origin, from `mac` met `meets` times,
  // as Copycat::decide. SelfEcho means this lamp yawned within the floor; it
  // spends no roll.
  CopyDecision catchYawn(const uint8_t mac[6], uint8_t disposition, SocialMode mode,
                         uint8_t frameHops, uint8_t meets, uint32_t nowMs, uint8_t chanceRoll,
                         uint32_t jitterRoll, size_t nearby, bool yawnedRecently,
                         const ContagionTuning& contagion);

  // Call at least once per lampos::kRetiredAgeMs.
  void retire(uint32_t nowMs) { rolls_.retire(nowMs, tuning.yawnRollCooldownMs); }

  YawnTuning tuning;

 private:
  RollCooldownTable rolls_;
  bool wasTired_ = false;
  uint32_t lastTiredRollMs_ = 0;
};

}  // namespace lamp
