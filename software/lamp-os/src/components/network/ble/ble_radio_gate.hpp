#pragma once

#include <atomic>
#include <mutex>

namespace ble_control {

// The advertising and central-scan controls BleRadioGate drives.
class BleRadio {
 public:
  virtual ~BleRadio() = default;
  virtual void startAdvertising() = 0;
  virtual void stopAdvertising() = 0;
  virtual void startScan() = 0;
  virtual void stopScan() = 0;
  virtual bool isAdvertising() = 0;
};

// Decides when the lamp's BLE advertising and central scan may run. After boot
// every firmware start of either goes through it. An OTA radio pause holds
// both; a connected GATT client holds the scan. Each call runs whole against
// the others, so a hold taken on one task is never undone by a restart racing
// it on another. Safe from NimBLE host-task callbacks, since no BleRadio call
// re-enters it.
class BleRadioGate {
 public:
  explicit BleRadioGate(BleRadio& radio) : radio_(radio) {}

  // Each returns true on the transition and does nothing when already there.
  bool pauseForOta();
  bool resumeAfterOta();

  void clientConnected();
  void clientDisconnected();
  // NimBLE ended the continuous scan on its own.
  void scanEnded();
  void openScanWindow();
  void closeScanWindow();
  // Re-emits the advertisement unless the OTA pause holds it.
  void restartAdvertising();
  // Stops advertising that NimBLE restarted on its own under the OTA pause (a
  // host reset re-sync does). Call periodically.
  void enforcePause();

  bool otaPaused() const { return otaPaused_.load(); }

 private:
  BleRadio&         radio_;
  std::mutex        mu_;
  std::atomic<bool> otaPaused_{false};
  bool              clientHoldsScan_ = false;
};

}  // namespace ble_control
