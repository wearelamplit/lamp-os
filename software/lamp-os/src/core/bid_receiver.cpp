#include <Arduino.h>  // fast_rng.hpp needs esp_random declared (transitive)

#include "core/bid_receiver.hpp"

#include <cstring>

#include "components/network/protocol/bid.hpp"
#include "core/social_tier.hpp"
#include "util/fast_rng.hpp"

namespace lamp {

bool BidReceiver::onBid(const uint8_t sourceMac[6], uint8_t bidType,
                        uint8_t disposition, SocialMode mode, uint32_t nowMs,
                        FastRng& rng) {
  if (bidType != lamp_protocol::BID_GREETING) return false;
  if (everHonored_ && (nowMs - lastHonorMs_) < kBidHonorCooldownMs) return false;

  const uint8_t p = socialBidResponse(disposition, mode);
  if (rng.range(0, 99) >= p) return false;

  armed_       = true;
  dueMs_       = nowMs + rng.range(0, kBidHonorJitterMs);
  std::memcpy(mac_, sourceMac, 6);
  bidType_     = bidType;
  everHonored_ = true;
  lastHonorMs_ = nowMs;
  return true;
}

bool BidReceiver::takeDue(uint32_t nowMs, uint8_t sourceMacOut[6],
                          uint8_t& bidTypeOut) {
  if (!armed_ || nowMs < dueMs_) return false;
  std::memcpy(sourceMacOut, mac_, 6);
  bidTypeOut = bidType_;
  armed_ = false;
  return true;
}

}  // namespace lamp
