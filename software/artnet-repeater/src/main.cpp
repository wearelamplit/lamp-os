#include <Arduino.h>
#include <AsyncUDP.h>
#include <NimBLEDevice.h>
#include <WiFi.h>

#include <cstdint>
#include <string>
#include <vector>

#include "./secrets.hpp"
#define ART_NET_PORT 6454
#define MAX_BUFFER_ARTNET 530
#define BLE_MAGIC_NUMBER 42007
#define MIN_UPDATE_TIME 250
#define IP_RANGE_START_ADDRESS 20
#define TOTAL_IP_COUNT 20

AsyncUDP udp;
uint32_t lastUpdate = 0;

void onWiFiEvent(WiFiEvent_t event) {
  Serial.printf("WIFI Event %d\n", event);
};

void setup() {
  // Select external antenna
  // @see https://github.com/espressif/arduino-esp32/commit/38d6ed5f12745bb990daa2e9802c91dc11e580bb
  digitalWrite(WIFI_ANT_CONFIG, HIGH);

  Serial.begin(115200);
  Serial.println("Initializing...");
  std::string coordinatorSsid = SECRET_COORDINATOR_SSID;
  std::string coordinatorPassword = SECRET_COORDINATOR_PASSWORD;

  NimBLEDevice::init(SECRET_COORDINATOR_STAGE_NAME);
  uint8_t result = NimBLEDevice::setPower(ESP_PWR_LVL_P9, NimBLETxPowerType::Advertise);
  Serial.printf("Setting bluetooth to +9dB with status code: %d\n", result);

  // Stage coordinators advertise the following packet
  // 2 bytes: coordinator identifier [Manufacturer ID block]
  // 26 bytes: a null terminated ssid and a null terminated password
  // combined password and ssid should not be more than 24 chars
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(SECRET_COORDINATOR_STAGE_NAME);
  pAdvertising->enableScanResponse(true);
  std::vector<unsigned char> data;
  data.reserve(28);
  std::vector<char> magicBytes{char(BLE_MAGIC_NUMBER & 0xff), char((BLE_MAGIC_NUMBER >> 8) & 0xff)};
  data.insert(data.end(), magicBytes.begin(), magicBytes.end());
  std::vector<char> ssidBytes(coordinatorSsid.c_str(), coordinatorSsid.c_str() + coordinatorSsid.size() + 1);
  data.insert(data.end(), ssidBytes.begin(), ssidBytes.end());
  std::vector<char> passwordBytes(coordinatorPassword.c_str(), coordinatorPassword.c_str() + coordinatorPassword.size() + 1);
  data.insert(data.end(), passwordBytes.begin(), passwordBytes.end());
  pAdvertising->setMinInterval(650);
  pAdvertising->setMaxInterval(800);
  pAdvertising->setManufacturerData(data);
  pAdvertising->setConnectableMode(0);
  pAdvertising->start();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWiFiEvent);
  WiFi.config(IPAddress(10, 0, 0, 2),
              IPAddress(10, 0, 0, 1),
              IPAddress(255, 255, 255, 0),
              IPAddress(10, 0, 0, 1));
  WiFi.begin(SECRET_COORDINATOR_SSID, SECRET_COORDINATOR_PASSWORD);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  udp.listen(ART_NET_PORT);
  udp.onPacket([](AsyncUDPPacket packet) {
    uint32_t packetSize = packet.length();

    if (packetSize == MAX_BUFFER_ARTNET) {
      uint32_t now = millis();

      if (now > lastUpdate + MIN_UPDATE_TIME) {
        lastUpdate = now;
        uint8_t* data = packet.data();
        for (int i = 0; i == TOTAL_IP_COUNT; i++) {
          udp.writeTo(data, MAX_BUFFER_ARTNET, IPAddress(10, 0, 0, i + IP_RANGE_START_ADDRESS), ART_NET_PORT, TCPIP_ADAPTER_IF_STA);
        }
      }
    }
  });
}

void loop() {}