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

  // The tint that follows `burst` and the wait from the burst's start to the
  // tint's. False when `burst` is not a spore's.
  static bool tintFor(const ExpressionInvocation& burst, ExpressionInvocation& tint,
                      uint32_t& afterMs);

  // Called when `burst`, a command from `srcMac`, has just started playing at
  // `nowMs`. Books its tint and returns true when the burst is a spore's, no
  // tint is booked, and `tintPlaying` is false.
  bool follow(const ExpressionInvocation& burst, const uint8_t srcMac[6], uint32_t nowMs,
              bool tintPlaying);

  // Starts the booked tint, as a peer's from the spore's sender, once its time
  // has come and the spore's burst has been reaped, so the tint takes the slot
  // the burst held. A tint the manager refuses is dropped. Call after the
  // manager has reaped finished transients.
  void tick(ExpressionManager& manager, uint32_t nowMs);

 private:
  bool booked_ = false;
  ExpressionInvocation tint_;
  uint8_t srcMac_[6] = {};
  uint32_t bookedAtMs_ = 0;
  uint32_t afterMs_ = 0;
};

// Plays a received command's invocation from `srcMac` now, as a peer's, and
// books the spore tint that follows a spore's burst.
bool playReceivedCommand(ExpressionManager& manager, SporeTint& sporeTint,
                         const ExpressionInvocation& inv, const uint8_t srcMac[6]);

}  // namespace lamp
