#pragma once
#include <cstdint>

enum wifi_second_chan_t { WIFI_SECOND_CHAN_NONE = 0 };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 };

namespace {
uint8_t g_mock_wifi_channel = 0;
}
inline int esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t) {
  g_mock_wifi_channel = primary;
  return 0;
}
