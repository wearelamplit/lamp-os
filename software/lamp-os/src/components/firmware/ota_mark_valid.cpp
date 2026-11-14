#include "components/firmware/ota_mark_valid.hpp"

namespace lamp {

MarkValidCheck otaMarkValid;

void MarkValidCheck::arm(bool pendingVerify, uint32_t nowMs) {
  pending_ = pendingVerify;
  asked_ = false;
  deadlineMs_ = nowMs + kSettleMs;
}

bool MarkValidCheck::due(uint32_t nowMs, bool bleRunning) {
  if (!pending_ || asked_ || static_cast<int32_t>(nowMs - deadlineMs_) < 0) return false;
  if (!bleRunning) {
    deadlineMs_ = nowMs + kRetryMs;
    return false;
  }
  asked_ = true;
  return true;
}

}  // namespace lamp
