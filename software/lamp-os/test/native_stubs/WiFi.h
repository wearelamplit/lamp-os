#pragma once
// Host stand-in for the Arduino WiFi global, enough for the scan state machine
// in components/network/transport/wifi.cpp. A test sets `aps` (strongest
// first, as the IDF orders a scan) and `scanResult`, the scanComplete() value.
#include <cstdint>
#include <string>
#include <vector>

#include "Arduino.h"
#include "esp_wifi.h"

enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA, WIFI_AP, WIFI_AP_STA };
constexpr int16_t WIFI_SCAN_RUNNING = -1;
constexpr int16_t WIFI_SCAN_FAILED = -2;

struct IPAddress {
  String toString() const { return "0.0.0.0"; }
};

struct MockAp {
  std::string ssid;
  int32_t rssi;
  wifi_auth_mode_t auth;
};

struct MockWiFi {
  std::vector<MockAp> aps;
  int16_t scanResult = WIFI_SCAN_RUNNING;
  int scansStarted = 0;

  void persistent(bool) {}
  bool disconnect(bool = false, bool = false) { return true; }
  bool mode(wifi_mode_t) { return true; }
  bool setSleep(bool) { return true; }
  int16_t scanNetworks(bool = false, bool = false) {
    ++scansStarted;
    scanResult = WIFI_SCAN_RUNNING;
    return WIFI_SCAN_RUNNING;
  }
  int16_t scanComplete() const { return scanResult; }
  void scanDelete() {}
  String SSID(uint8_t i) const { return aps[i].ssid; }
  int32_t RSSI(uint8_t i) const { return aps[i].rssi; }
  wifi_auth_mode_t encryptionType(uint8_t i) const { return aps[i].auth; }
  bool softAP(const char*, const char*, int, int, int) { return true; }
  bool softAPdisconnect(bool) { return true; }
  IPAddress softAPIP() const { return {}; }
};

inline MockWiFi WiFi;
