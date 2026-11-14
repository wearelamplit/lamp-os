#pragma once

#include <cstdint>

namespace lamp {

// The post-update self-check. An image the bootloader booted for the first time
// (PENDING_VERIFY) is marked valid once its loop has run kSettleMs with BLE up;
// until then any reset boots the previous image, and the lamp takes no
// firmware or filesystem offer. An image whose BLE is still down kBleWaitMs
// after the settle window is rolled back. See docs/dev/embedded-heap.md
// "Rollback after an update".
class MarkValidCheck {
 public:
  static constexpr uint32_t kSettleMs = 30000;
  static constexpr uint32_t kRetryMs = 5000;
  static constexpr uint32_t kBleWaitMs = 120000;

  enum class Step : uint8_t { Wait, MarkValid, RollBack };

  // End of setup. Arms only when the running image is unverified.
  void arm(bool pendingVerify, uint32_t nowMs);

  // Loop, every iteration. MarkValid or RollBack at most once per kRetryMs
  // until validated(), so a failed mark or rollback is asked again.
  Step poll(uint32_t nowMs, bool bleRunning);

  // The mark succeeded.
  void validated() { pending_ = false; }

  // While true, the lamp declines every OTA offer: a firmware image is written
  // raw over the slot a rollback returns to.
  bool pending() const { return pending_; }

 private:
  bool pending_ = false;
  uint32_t settledMs_ = 0;
  uint32_t nextMs_ = 0;
};

extern MarkValidCheck otaMarkValid;

// The running image is one the bootloader has not yet seen marked valid.
bool runningImagePendingVerify();

}  // namespace lamp
