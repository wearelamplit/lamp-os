#include "components/firmware/ota_mark_valid.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#endif

namespace lamp {

MarkValidCheck otaMarkValid;

void MarkValidCheck::arm(bool pendingVerify, uint32_t nowMs, bool bleRunning) {
  pending_ = pendingVerify;
  bleRan_ = bleRunning;
  settledMs_ = nowMs + kSettleMs;
  nextMs_ = settledMs_;
}

MarkValidCheck::Step MarkValidCheck::poll(uint32_t nowMs, bool bleRunning) {
  bleRan_ = bleRan_ || bleRunning;
  if (!pending_ || static_cast<int32_t>(nowMs - nextMs_) < 0) return Step::Wait;
  nextMs_ = nowMs + kRetryMs;
  if (bleRan_) return Step::MarkValid;
  return nowMs - settledMs_ >= kBleWaitMs ? Step::RollBack : Step::Wait;
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

bool runningImagePendingVerify() {
  esp_ota_img_states_t state;
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

void rollBackToPreviousImage() {
  Serial.flush();
  const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
  Serial.printf("[ota] rollback refused (0x%X), restarting\n", static_cast<unsigned>(err));
  Serial.flush();
  esp_restart();
}

#endif

}  // namespace lamp

#if defined(ARDUINO) || defined(ESP_PLATFORM)
// Overrides the Arduino core's weak hook so initArduino() leaves a
// PENDING_VERIFY image unverified and MarkValidCheck decides. Without
// extern "C" this defines an unrelated C++ symbol, and every update is marked
// valid before setup().
extern "C" bool verifyRollbackLater() { return true; }
#endif
