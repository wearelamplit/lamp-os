#include <Arduino.h>
#include <AsyncUDP.h>
#include <ETH.h>
#include <NimBLEDevice.h>
#include <SPI.h>

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

#ifndef ETH_PHY_CS
#define ETH_PHY_TYPE ETH_PHY_W5500
#define ETH_PHY_ADDR 1
#define ETH_PHY_CS D3
#define ETH_PHY_IRQ -1
#define ETH_PHY_RST -1
#endif
// SPI pins
#define ETH_SPI_SCK D8
#define ETH_SPI_MISO D9
#define ETH_SPI_MOSI D10
#define ETH_SPI_FREQ 20

AsyncUDP udp;
uint32_t lastUpdate = 0;
static bool eth_connected = false;

// Handle Ethernet Events:
void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      // This will happen during setup, when the Ethernet service starts
      Serial.println("ETH Started");
      // set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;

    case ARDUINO_EVENT_ETH_CONNECTED:
      // This will happen when the Ethernet cable is plugged
      Serial.println("ETH Connected");
      break;

    case ARDUINO_EVENT_ETH_GOT_IP:
      // This will happen when we obtain an IP address through DHCP:
      Serial.print("Got an IP Address for ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;

      // Uncomment to automatically make a test connection to a server:
      // testClient( "192.168.0.1", 80 );

      break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
      // This will happen when the Ethernet cable is unplugged
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;

    case ARDUINO_EVENT_ETH_STOP:
      // This will happen when the ETH interface is stopped but this never happens
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;

    default:
      break;
  }
}

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

  Network.onEvent(onEvent);
  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI);
  ETH.setHostname("artnet-repeater");
  ETH.config(IPAddress(10, 0, 0, 2),
             IPAddress(10, 0, 0, 1),
             IPAddress(255, 255, 255, 0),
             IPAddress(10, 0, 0, 1));
  Serial.println("Waiting for Ethernet connection");
  while (!eth_connected) {
    delay(500);
    Serial.print(".");
  }
  udp.listen(ART_NET_PORT);
  udp.onPacket([](AsyncUDPPacket packet) {
    uint32_t packetSize = packet.length();

    if (packetSize == MAX_BUFFER_ARTNET) {
      Serial.println(packetSize);
      uint32_t now = millis();

      if (now > lastUpdate + MIN_UPDATE_TIME) {
        lastUpdate = now;
        uint8_t* data = packet.data();
        // for (int i = 0; i == TOTAL_IP_COUNT; i++) {
        //   udp.writeTo(data, MAX_BUFFER_ARTNET, IPAddress(10, 0, 0, i + IP_RANGE_START_ADDRESS), ART_NET_PORT, TCPIP_ADAPTER_IF_ETH);
        // }
      }
    }
  });
}

void testClient(const char* host, uint16_t port) {
  Serial.print("\nconnecting to ");
  Serial.println(host);

  NetworkClient client;
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    return;
  }
  client.printf("GET / HTTP/1.1\r\nHost: %s\r\n\r\n", host);
  while (client.connected() && !client.available());
  while (client.available()) {
    Serial.write(client.read());
  }

  Serial.println("closing connection\n");
  client.stop();
}

void loop() {
  if (eth_connected) {
    testClient("maestro.local", 80);
  }
  delay(1000);
}