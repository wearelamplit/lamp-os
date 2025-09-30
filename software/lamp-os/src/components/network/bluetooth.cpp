
/**
 *  Lamp Bluetooth Management
 *
 *  Based on: NimBLE_Async_client Demo
 *
 *  Demonstrates asynchronous client operations.
 *
 *  Created: on November 4, 2024
 *      Author: H2zero
 */
#include "./bluetooth.hpp"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <string>
#include <vector>

#include "../../util/color.hpp"
#include "./bluetooth_pool.hpp"

namespace lamp {
BluetoothPool lampBluetoothPool;

class ScanCallbacks : public NimBLEScanCallbacks {
  bool isStage(std::string data) {
    return (data.length() > 1 &&
            data[0] == (BLE_STAGE_MAGIC_NUMBER & 0xff) &&
            data[1] == ((BLE_STAGE_MAGIC_NUMBER >> 8) & 0xff));
  };

  bool isLamp(std::string data) {
    return (data.length() == 8 &&
            data[0] == (BLE_LAMP_MAGIC_NUMBER & 0xff) &&
            data[1] == ((BLE_LAMP_MAGIC_NUMBER >> 8) & 0xff));
  };

  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    if (advertisedDevice->haveName() && advertisedDevice->haveManufacturerData()) {
      std::string data = advertisedDevice->getManufacturerData();

      if (advertisedDevice->getRSSI() > BLE_MINIMUM_RSSI_VALUE && isStage(data)) {
        // parse mfg message
        // 2 bytes - stage ID
        // 26 bytes - 2 null terminated strings
        String ssid;
        String password;
        int i = 2;
        for (; i < data.size(); i++) {
          if (data[i] == 0) {
            i++;
            break;
          }
          ssid.concat((char)data[i]);
        }

        for (; i < data.size(); i++) {
          if (data[i] == 0) {
            break;
          }

          password.concat((char)data[i]);
        }

        auto stage = BluetoothStageRecord(
            advertisedDevice->getName(),
            ssid,
            password,
            millis());

        lampBluetoothPool.addOrUpdateStage(stage);
      }

      if (advertisedDevice->getRSSI() > BLE_MINIMUM_RSSI_VALUE && isLamp(data)) {
        auto lamp = BluetoothLampRecord(
            advertisedDevice->getName(),
            Color(data[2], data[3], data[4], 0),
            Color(data[5], data[6], data[7], 0),
            millis());
        lampBluetoothPool.addOrUpdateLamp(lamp);
      }
    }
  };

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
#ifdef LAMP_DEBUG
    std::vector<BluetoothLampRecord> lampsFound = lampBluetoothPool.getLamps();
    int i = 0;
    for (i = 0; i < lampsFound.size(); i++) {
      Serial.printf("Lamp Name: %s time found: %ld - acknowledged: %d - ",
                    lampsFound[i].name.c_str(),
                    lampsFound[i].lastSeenTimeMs,
                    lampsFound[i].acknowledged);
      Serial.printf("color base: #%02x%02x%02x - ", lampsFound[i].baseColor.r, lampsFound[i].baseColor.g, lampsFound[i].baseColor.b);
      Serial.printf("color shade: #%02x%02x%02x\n", lampsFound[i].shadeColor.r, lampsFound[i].shadeColor.g, lampsFound[i].shadeColor.b);
    }

    std::vector<BluetoothStageRecord> stagesFound = lampBluetoothPool.getStages();
    for (i = 0; i < stagesFound.size(); i++) {
      Serial.printf("Stage Name: %s time found: %ld - ssid: %s - pass: %s\n",
                    stagesFound[i].name.c_str(),
                    stagesFound[i].lastSeenTimeMs,
                    stagesFound[i].ssid.c_str(),
                    stagesFound[i].password.c_str());
    }
#endif
    lampBluetoothPool.pruneLamps();
    lampBluetoothPool.pruneStages();
    NimBLEDevice::getScan()->start(BLE_GAP_SCAN_TIME_MS);
  }
} scanCallbacks;

BluetoothComponent::BluetoothComponent() {};

void BluetoothComponent::begin(std::string name, Color inBaseColor,
                               Color inShadeColor) {
#ifdef LAMP_DEBUG
  Serial.printf("Starting Bluetooth Async Client\n");
#endif
  NimBLEDevice::init(name.substr(0, 12));
  NimBLEDevice::setPower(BLE_POWER_LEVEL);

  // Scan for all bluetooth devices and filter the list
  // for lamps by manufacturer ID. If a lamp is found, add
  // it to the found lamps buffer
  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks);
  pScan->setInterval(BLE_GAP_ADV_INTERVAL_MS);
  pScan->setWindow(BLE_GAP_SCAN_WINDOW_MS);
  pScan->setActiveScan(true);
  pScan->start(BLE_GAP_SCAN_TIME_MS);

  // Lamps advertise 8 bytes of manufacturer data to share colors
  // 2 bytes: lamp identifier [Manufacturer ID block]
  // 3 bytes: RGB base color (white omitted)
  // 3 bytes: RGB shade color (white omitted)
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(name);
  pAdvertising->enableScanResponse(true);
  std::vector<unsigned char> data{
      static_cast<unsigned char>(BLE_LAMP_MAGIC_NUMBER & 0xff),
      static_cast<unsigned char>((BLE_LAMP_MAGIC_NUMBER >> 8) & 0xff),
      static_cast<unsigned char>(inBaseColor.r),
      static_cast<unsigned char>(inBaseColor.g),
      static_cast<unsigned char>(inBaseColor.b),
      static_cast<unsigned char>(inShadeColor.r),
      static_cast<unsigned char>(inShadeColor.g),
      static_cast<unsigned char>(inShadeColor.b),
  };
  pAdvertising->setManufacturerData(data);
  pAdvertising->setConnectableMode(0);
  pAdvertising->setMinInterval(BLE_ADVERTISING_INTERVAL_MIN);
  pAdvertising->setMaxInterval(BLE_ADVERTISING_INTERVAL_MAX);
  pAdvertising->start();
};

std::vector<BluetoothLampRecord> *BluetoothComponent::getLamps() {
  return &lampBluetoothPool.lampPool;
};

std::vector<BluetoothStageRecord> *BluetoothComponent::getStages() {
  return &lampBluetoothPool.stagePool;
};
}  // namespace lamp