#pragma once

#include <cstddef>
#include <cstdint>

#include "behaviors/copycat.hpp"
#include "config/disposition_store.hpp"
#include "core/greeting_profiles.hpp"
#include "expressions/expression_invocation.hpp"
#include "util/color.hpp"

namespace lamp {

// Reunion greeting and colour hint feel numbers, live-tunable over serial on a
// debug build. Mechanism: docs/dev/social.md, "Reunion and colour hint".
struct ReunionTuning {
  // A Smitten peer unseen this many on-minutes is greeted as a reunion.
  uint16_t reunionMin = 240;
  // A reunion holds this many times as long, so it breathes as many times more.
  uint8_t reunionX = 2;
  // One colour hint roll per window, whether or not it played.
  uint32_t hintCooldownMs = 600000;
  // The colour hint's peak, in percent of the friend's colour.
  uint8_t hintOpacity = 30;
};

inline constexpr uint8_t kMaxReunionX = 4;
inline constexpr uint32_t kMaxHintCooldownMs = 3600000;
inline constexpr uint8_t kMinHintOpacity = 10;
inline constexpr uint8_t kMaxHintOpacity = 100;
// The lowest disposition whose colour a lamp borrows: Fond.
inline constexpr uint8_t kHintMinDisposition = 4;
// The colour hint's one breath out and back, in seconds (the breathing floor).
inline constexpr uint32_t kHintBreathSeconds = 8;

// True when greeting a peer whose row reads `peer` is a reunion. Read the row
// before markSeen restarts its unseen clock.
bool isReunion(const PeerRecord& peer, const ReunionTuning& t);

// `base` with its hold `x` times as long. Every Smitten profile breathes
// through its whole hold, so its breaths multiply with it.
GreetingTuning reunionGreeting(const GreetingTuning& base, uint8_t x);

// Sets ReunionTuning field `key` (its member name) to `value`. False, with `t`
// unchanged, for an unknown key or a value out of range.
bool setReunionTuning(ReunionTuning& t, const char* key, uint32_t value);

// The colour hint for a friend whose colour is `colour`: faint breaths on the
// shade, played Local and never announced.
ExpressionInvocation colourHintInvocation(const Color& colour, uint8_t opacityPct);

// Whether the end of a greeting borrows the greeted friend's colour. Fixed RAM.
class ColourHint {
 public:
  // A peer at `disposition` from Fond up rolls once per t.hintCooldownMs,
  // whatever the outcome, at 100% scaled by contagion's crowd damping over
  // `nearby` lamps. `chanceRoll` is a 1..100 draw.
  bool roll(uint8_t disposition, size_t nearby, uint8_t chanceRoll, uint64_t nowMs,
            const ReunionTuning& t, const ContagionTuning& contagion);

 private:
  bool rolled_ = false;
  uint64_t lastRollMs_ = 0;
};

}  // namespace lamp
