#pragma once

#include <cstdint>

namespace lamp {

// The post-update self-check. An image the bootloader booted for the first time
// (PENDING_VERIFY) is marked valid once its loop has run kSettleMs and BLE has
// run at some point this boot, so the web app taking BLE down does not count
// against it. Until then any reset boots the previous image. An image whose
// BLE has not run kBleWaitMs after the settle window is rolled back. See
// docs/dev/embedded-heap.md "Rollback after an update".
class MarkValidCheck {
 public:
  static constexpr uint32_t kSettleMs = 30000;
  static constexpr uint32_t kRetryMs = 5000;
  static constexpr uint32_t kBleWaitMs = 120000;

  enum class Step : uint8_t { Wait, MarkValid, RollBack };

  // End of setup. Arms only when the running image is unverified.
  void arm(bool pendingVerify, uint32_t nowMs, bool bleRunning);

  // Loop, every iteration. MarkValid or RollBack at most once per kRetryMs
  // until validated(), so a failed mark is asked again.
  Step poll(uint32_t nowMs, bool bleRunning);

  // The mark succeeded.
  void validated() { pending_ = false; }

  // While true, the lamp takes no firmware or filesystem offer, since a
  // firmware image is written raw over the slot a rollback returns to, and
  // offers neither image to its peers.
  bool pending() const { return pending_; }

 private:
  bool pending_ = false;
  bool bleRan_ = false;
  uint32_t settledMs_ = 0;
  uint32_t nextMs_ = 0;
};

extern MarkValidCheck otaMarkValid;

// The running image is one the bootloader has not yet seen marked valid.
bool runningImagePendingVerify();

// Restarts into the previous image. Restarts even when IDF refuses to mark the
// running image invalid, since the bootloader sets an unverified image aside
// on any reset.
[[noreturn]] void rollBackToPreviousImage();

}  // namespace lamp
