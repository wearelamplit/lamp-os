#pragma once

#include <cstdint>

#include "expressions/expression_invocation.hpp"

namespace lamp {

class ExpressionManager;

// A mushroom's spore reaches a lamp as a received command carrying a tinted
// flicker burst with `fadeDuration`. After the burst the lamp tints its base
// in the spore's colour and fades back over fadeDuration seconds, one tint at
// a time (docs/dev/moments.md, "Spore tint").
class SporeTint {
 public:
  static constexpr uint32_t kFadeS = 4;
  static constexpr uint32_t kHoldS = 20;
  static constexpr uint32_t kOpacityPct = 80;
  static constexpr uint32_t kMaxFadeBackS = 100;

  // For `burst`, a command's invocation that just started playing at `nowMs`:
  // when it is a spore's and no tint is booked or playing, books the tint,
  // sets `tint` to it and `afterMs` to the wait until the burst ends, and
  // returns true.
  bool follow(const ExpressionInvocation& burst, uint32_t nowMs, ExpressionInvocation& tint,
              uint32_t& afterMs);

 private:
  bool booked_ = false;
  uint32_t bookedAtMs_ = 0;
  uint32_t busyMs_ = 0;
};

// Plays a received command's invocation from `srcMac` now, as a peer's, and
// queues the spore tint that follows a spore's burst.
bool playReceivedCommand(ExpressionManager& manager, SporeTint& sporeTint,
                         const ExpressionInvocation& inv, const uint8_t srcMac[6]);

}  // namespace lamp
