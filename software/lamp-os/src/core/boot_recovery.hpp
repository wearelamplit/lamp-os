#pragma once

#include <cstdint>

namespace lamp {

// BOOT-button password recovery. GPIO0 is a strapping pin: held low at reset
// the ROM bootloader enters serial-download mode and this firmware never runs,
// so the hold only ever registers while the lamp is already running. The
// window bounds it to the first minute of uptime, keeping a stray GPIO0 pull
// during a long session from silently clearing the password.
constexpr uint32_t kBootRecoveryWindowMs = 60000;
constexpr uint32_t kBootRecoveryHoldMs = 5000;

// Accumulates a continuous BOOT hold and fires once per boot. `pressed` is the
// logical button state (GPIO0 is active-low, so the caller inverts);
// `uptimeMs` is milliseconds since boot from a clock that never wraps.
class BootRecovery {
 public:
  bool update(bool pressed, uint64_t uptimeMs) {
    if (fired_) return false;
    if (!pressed) {
      holdStartMs_ = 0;
      return false;
    }
    // Only the START of the hold is gated, so a press begun just inside the
    // window still completes.
    if (holdStartMs_ == 0) {
      if (uptimeMs > kBootRecoveryWindowMs) return false;
      holdStartMs_ = uptimeMs == 0 ? 1 : uptimeMs;
      return false;
    }
    if (uptimeMs - holdStartMs_ < kBootRecoveryHoldMs) return false;
    fired_ = true;
    return true;
  }

  uint64_t heldMs(uint64_t uptimeMs) const {
    return holdStartMs_ == 0 ? 0 : uptimeMs - holdStartMs_;
  }

 private:
  uint64_t holdStartMs_ = 0;
  bool fired_ = false;
};

}  // namespace lamp
