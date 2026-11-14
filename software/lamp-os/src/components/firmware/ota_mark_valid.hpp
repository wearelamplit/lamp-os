#pragma once

#include <cstdint>

namespace lamp {

// The post-update self-check. An image the bootloader booted for the first time
// (PENDING_VERIFY) is marked valid once its loop has run kSettleMs with BLE up;
// until then any reset boots the previous image. Arduino marks every image
// valid before setup() unless the C hook verifyRollbackLater() returns true, so
// this check arms only once `extern "C" bool verifyRollbackLater()` is defined.
// See docs/dev/embedded-heap.md "Rollback after an update".
class MarkValidCheck {
 public:
  static constexpr uint32_t kSettleMs = 30000;
  static constexpr uint32_t kRetryMs = 5000;

  // End of setup. Arms only when the running image is unverified.
  void arm(bool pendingVerify, uint32_t nowMs);

  // Loop, every iteration. True once, when the image should be marked valid
  // now; without BLE up it waits kRetryMs and asks again.
  bool due(uint32_t nowMs, bool bleRunning);

  // The mark succeeded. A failed mark leaves the image pending.
  void validated() { pending_ = false; }

  bool pending() const { return pending_; }

 private:
  bool pending_ = false;
  bool asked_ = false;
  uint32_t deadlineMs_ = 0;
};

// A firmware image is written raw into the slot that holds the previous image,
// so an unverified lamp takes none until it is marked valid. A filesystem image
// goes to its own partition.
constexpr bool offerWaitsForMarkValid(bool filesystemOffer, bool pendingVerify) {
  return !filesystemOffer && pendingVerify;
}

extern MarkValidCheck otaMarkValid;

}  // namespace lamp
