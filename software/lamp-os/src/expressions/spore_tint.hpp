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
  // The tint starts this long after the burst ends, so the burst has been
  // reaped and its Remote slot is free for the tint.
  static constexpr uint32_t kAfterBurstMs = 250;
  // A booked tint counts as booked this long past its start time, covering
  // the loop pass between a command's drain and the queued tint's fire.
  static constexpr uint32_t kFireGraceMs = 1000;

  // Called when `burst`, a command's invocation, has just started playing at
  // `nowMs`. Returns true, books the tint and sets `tint` and `afterMs` (the
  // wait before it starts) when the burst is a spore's, no tint is booked,
  // and `tintPlaying` is false.
  bool follow(const ExpressionInvocation& burst, uint32_t nowMs, bool tintPlaying,
              ExpressionInvocation& tint, uint32_t& afterMs);

 private:
  bool booked_ = false;
  uint32_t bookedAtMs_ = 0;
  uint32_t bookedForMs_ = 0;
};

// Plays a received command's invocation from `srcMac` now, as a peer's, and
// queues the spore tint that follows a spore's burst.
bool playReceivedCommand(ExpressionManager& manager, SporeTint& sporeTint,
                         const ExpressionInvocation& inv, const uint8_t srcMac[6]);

}  // namespace lamp
