#include "components/firmware/ota_mark_valid.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <esp_ota_ops.h>
#endif

namespace lamp {

MarkValidCheck otaMarkValid;

void MarkValidCheck::arm(bool pendingVerify, uint32_t nowMs) {
  pending_ = pendingVerify;
  settledMs_ = nowMs + kSettleMs;
  nextMs_ = settledMs_;
}

MarkValidCheck::Step MarkValidCheck::poll(uint32_t nowMs, bool bleRunning) {
  if (!pending_ || static_cast<int32_t>(nowMs - nextMs_) < 0) return Step::Wait;
  nextMs_ = nowMs + kRetryMs;
  if (bleRunning) return Step::MarkValid;
  return nowMs - settledMs_ >= kBleWaitMs ? Step::RollBack : Step::Wait;
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

bool runningImagePendingVerify() {
  esp_ota_img_states_t state;
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

#endif

}  // namespace lamp

#if defined(ARDUINO) || defined(ESP_PLATFORM)
// Overrides the Arduino core's weak hook so initArduino() leaves a
// PENDING_VERIFY image unverified and MarkValidCheck decides. The hook is C:
// without extern "C" this defines an unrelated C++ symbol and every update is
// marked valid before setup().
extern "C" bool verifyRollbackLater() { return true; }
#endif
