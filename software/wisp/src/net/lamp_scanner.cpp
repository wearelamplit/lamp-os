#include "net/lamp_scanner.hpp"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <string>

namespace wisp {

namespace {
constexpr uint16_t kLampMagic = 42069;
constexpr int kRssiGate = -85;
// NimBLE-Arduino 2.x takes these in milliseconds. ~10% duty leaves airtime
// for ESP-NOW under SW-coex.
constexpr uint16_t kScanWindowMs = 30;
constexpr uint16_t kScanIntervalMs = 300;

class OldLampCallbacks : public NimBLEScanCallbacks {
 public:
  void bind(volatile uint32_t* stamp, volatile bool* seen) {
    stamp_ = stamp;
    seen_ = seen;
  }

  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (!stamp_) return;
    if (!dev->haveManufacturerData()) return;
    if (dev->getRSSI() <= kRssiGate) return;
    const std::string mfg = dev->getManufacturerData();
    if (!isOldLampBeacon(reinterpret_cast<const uint8_t*>(mfg.data()),
                         mfg.size())) {
      return;
    }
    *stamp_ = millis();
    *seen_ = true;
  }

 private:
  volatile uint32_t* stamp_ = nullptr;
  volatile bool* seen_ = nullptr;
};

OldLampCallbacks s_callbacks;
}  // namespace

bool isOldLampBeacon(const uint8_t* data, size_t len) {
  if (len != 8) return false;
  return data[0] == (kLampMagic & 0xff) &&
         data[1] == ((kLampMagic >> 8) & 0xff);
}

void LampScanner::begin() {
  s_callbacks.bind(&lastOldLampSeenMs_, &sawOldLamp_);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&s_callbacks);
  scan->setActiveScan(true);
  scan->setInterval(kScanIntervalMs);
  scan->setWindow(kScanWindowMs);
  scan->setMaxResults(0);  // callback-only; don't accumulate a result list
}

void LampScanner::startScan() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) return;
  scan->start(0);  // 0 = continuous
}

void LampScanner::stopScan() {
  NimBLEDevice::getScan()->stop();
}

bool LampScanner::sawOldLampWithin(uint32_t ms) const {
  if (!sawOldLamp_) return false;
  return millis() - lastOldLampSeenMs_ < ms;
}

}  // namespace wisp
