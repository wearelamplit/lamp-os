#include "ble_radio_gate.hpp"

namespace ble_control {

bool BleRadioGate::pauseForOta() {
  std::lock_guard<std::mutex> lock(mu_);
  if (otaPaused_) return false;
  otaPaused_ = true;
  radio_.stopAdvertising();
  radio_.stopScan();
  return true;
}

bool BleRadioGate::resumeAfterOta() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!otaPaused_) return false;
  otaPaused_ = false;
  if (!clientHoldsScan_) radio_.startScan();
  radio_.startAdvertising();
  return true;
}

void BleRadioGate::clientConnected() {
  std::lock_guard<std::mutex> lock(mu_);
  clientHoldsScan_ = true;
  radio_.stopScan();
}

void BleRadioGate::clientDisconnected() {
  std::lock_guard<std::mutex> lock(mu_);
  clientHoldsScan_ = false;
  if (otaPaused_) return;
  radio_.startScan();
  radio_.startAdvertising();
}

void BleRadioGate::scanEnded() {
  std::lock_guard<std::mutex> lock(mu_);
  if (otaPaused_ || clientHoldsScan_) return;
  radio_.startScan();
}

void BleRadioGate::openScanWindow() {
  std::lock_guard<std::mutex> lock(mu_);
  if (otaPaused_) return;
  radio_.startScan();
}

void BleRadioGate::closeScanWindow() {
  std::lock_guard<std::mutex> lock(mu_);
  if (otaPaused_ || clientHoldsScan_) radio_.stopScan();
}

void BleRadioGate::restartAdvertising() {
  std::lock_guard<std::mutex> lock(mu_);
  if (otaPaused_) return;
  radio_.stopAdvertising();
  radio_.startAdvertising();
}

}  // namespace ble_control
