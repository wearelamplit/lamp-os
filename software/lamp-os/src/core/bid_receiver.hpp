#pragma once

#include <cstdint>

#include "config/config_types.hpp"  // SocialMode

namespace lamp { class FastRng; }

// Receiver-side bid gate + storm control, Core 1. A drained MSG_BID is rolled
// against socialBidResponse(disposition, mode); on accept a single deferred
// honor is armed with a randomized jitter delay so a crowd's honors (and the
// color-queries their greetings issue) scatter across ticks instead of firing
// on one frame. A per-receiver cooldown caps this lamp's contribution to a
// storm. Clock + rng are injected so native tests drive it deterministically.

namespace lamp {

class BidReceiver {
 public:
  // Roll acceptance for a freshly-drained bid and, on accept, arm a jittered
  // deferred honor (overwriting any un-fired one). Returns true when armed.
  // Only BID_GREETING is honored; an unknown bidType is ignored. A honor
  // within kBidHonorCooldownMs of the previous arm is suppressed.
  bool onBid(const uint8_t sourceMac[6], uint8_t bidType, uint8_t disposition,
             SocialMode mode, uint32_t nowMs, FastRng& rng);

  // Fill sourceMac/bidType + return true when an armed honor's jitter delay
  // has elapsed at nowMs, then clear the slot. The caller fires the greeting
  // (guarded by an inactive-greeting check + a fresh roster lookup).
  bool takeDue(uint32_t nowMs, uint8_t sourceMacOut[6], uint8_t& bidTypeOut);

  // Bench-tunable. Cooldown bounds honor rate; jitter spreads the honor burst.
  static constexpr uint32_t kBidHonorCooldownMs = 4000;
  static constexpr uint32_t kBidHonorJitterMs   = 400;

 private:
  bool     armed_       = false;
  uint32_t dueMs_       = 0;
  uint8_t  mac_[6]      = {0};
  uint8_t  bidType_     = 0;
  bool     everHonored_ = false;
  uint32_t lastHonorMs_ = 0;
};

}  // namespace lamp
