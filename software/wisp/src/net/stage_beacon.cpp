#include "net/stage_beacon.hpp"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <vector>

#include "config/wisp_config.hpp"

namespace wisp {

namespace {
constexpr uint16_t kStageMagic = 42007;
constexpr size_t kMaxAdvertBytes = 28;  // matches Gen 1 repeater
}  // namespace

void StageBeacon::begin(const std::string& deviceName, WispConfig* config) {
  if (initialized_) return;
  deviceName_ = deviceName;
  config_ = config;
  NimBLEDevice::init(deviceName_);
  // NimBLE-Arduino 2.3.6 takes dBm as int8_t, not the legacy esp_power_level_t
  // enum. Passing ESP_PWR_LVL_P9 (enum=7) here would yield +6 dBm — the Gen 1
  // repeater code had this bug. Pass 9 to actually get +9 dBm.
  NimBLEDevice::setPower(static_cast<int8_t>(9), NimBLETxPowerType::Advertise);
  initialized_ = true;
}

void StageBeacon::refreshAdvert() {
  if (!initialized_) {
    Serial.println("[stage] refreshAdvert() before begin(); ignoring");
    return;
  }
  // Pull the current creds straight from the source-of-truth. Empty
  // strings (no creds saved) collapse to a stop() so pre-mesh lamps
  // don't try to join an empty/garbage advert.
  const std::string ssid =
      config_ ? std::string(config_->wifiSsid().c_str()) : std::string();
  const std::string password =
      config_ ? std::string(config_->wifiPw().c_str()) : std::string();
  if (ssid.empty() || password.empty()) {
    stop();
    return;
  }

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();
  adv->setName(deviceName_);
  adv->enableScanResponse(true);
  adv->setConnectableMode(0);  // non-connectable
  adv->setMinInterval(650);
  adv->setMaxInterval(800);

  std::vector<uint8_t> data;
  data.reserve(kMaxAdvertBytes);
  // Manufacturer ID (LE)
  data.push_back(uint8_t(kStageMagic & 0xff));
  data.push_back(uint8_t((kStageMagic >> 8) & 0xff));
  // SSID + NUL
  for (char c : ssid) data.push_back(uint8_t(c));
  data.push_back(0);
  // Password + NUL
  for (char c : password) data.push_back(uint8_t(c));
  data.push_back(0);
  // Refuse — truncation can chop the SSID's NUL terminator and the lamp-side
  // scanner would then parse "ssidpassword" as the SSID with no password.
  // Better to fail loudly than to advertise broken creds.
  if (data.size() > kMaxAdvertBytes) {
    Serial.printf("[stage] mfg data too large (%u bytes, max %u). "
                  "ssid+password+2 NULs must fit in %u bytes. NOT advertising.\n",
                  (unsigned)data.size(), (unsigned)kMaxAdvertBytes,
                  (unsigned)(kMaxAdvertBytes - 2));
    advertising_ = false;
    return;
  }
  // NimBLE expects std::vector<unsigned char>
  std::vector<unsigned char> mfg(data.begin(), data.end());
  adv->setManufacturerData(mfg);

  adv->start();
  advertising_ = true;
  Serial.printf("[stage] advertising ssid='%s' (%u byte payload)\n",
                ssid.c_str(), (unsigned)data.size());
}

void StageBeacon::stop() {
  if (!initialized_) return;
  NimBLEDevice::getAdvertising()->stop();
  advertising_ = false;
}

}  // namespace wisp
