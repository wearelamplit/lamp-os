#include "net/stage_beacon.hpp"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <vector>

#include "config/wisp_config.hpp"

namespace wisp {

namespace {
constexpr uint16_t kStageMagic = 42007;
constexpr size_t kMaxAdvertBytes = 28;  // matches Gen 1 repeater
// Short so the name AD plus the creds fit one 31-byte advert. Lamps require a
// name to be present but only use it as a display label.
constexpr char kBeaconName[] = "wisp";
}  // namespace

void StageBeacon::begin(const std::string& deviceName, WispConfig* config) {
  if (initialized_) return;
  deviceName_ = deviceName;
  config_ = config;
  NimBLEDevice::init(deviceName_);
  // NimBLE 2.x takes raw dBm, not esp_power_level_t; 9 = +9 dBm.
  NimBLEDevice::setPower(static_cast<int8_t>(9), NimBLETxPowerType::Advertise);
  initialized_ = true;
}

void StageBeacon::refreshAdvert() {
  if (!initialized_) {
    Serial.println("[stage] refreshAdvert() before begin(); ignoring");
    return;
  }
  // Pull the current creds straight from the source-of-truth.
  const std::string ssid =
      config_ ? std::string(config_->wifiSsid().c_str()) : std::string();
  const std::string password =
      config_ ? std::string(config_->wifiPw().c_str()) : std::string();
  startAdvert(ssid, password);
}

void StageBeacon::advertiseCreds(const std::string& ssid,
                                 const std::string& password) {
  if (!initialized_) {
    Serial.println("[stage] advertiseCreds() before begin(); ignoring");
    return;
  }
  startAdvert(ssid, password);
}

void StageBeacon::startAdvert(const std::string& ssid,
                              const std::string& password) {
  // Empty strings (no creds) collapse to a stop() so pre-mesh lamps don't try
  // to join an empty/garbage advert.
  if (ssid.empty() || password.empty()) {
    stop();
    return;
  }

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();
  adv->setName(kBeaconName);
  adv->enableScanResponse(false);
  adv->setConnectableMode(0);  // non-connectable
  adv->setMinInterval(1600);
  adv->setMaxInterval(2000);

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
  // Truncation can chop the SSID's NUL terminator and the lamp-side
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
