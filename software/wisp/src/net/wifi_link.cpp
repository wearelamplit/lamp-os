#include "net/wifi_link.hpp"

#include <WiFi.h>
#include <esp_wifi.h>

#include "config/wisp_config.hpp"
#include "net/mesh_link.hpp"  // LAMP_ESPNOW_CHANNEL

namespace wisp {

void WifiLink::begin(WispConfig* config) {
  if (started_) return;
  started_ = true;
  config_ = config;
  mode_ = Mode::Sta;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, [[maybe_unused]] WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.printf("[wifi] got IP: %s\n",
                    WiFi.localIP().toString().c_str());
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.println("[wifi] disconnected");
#ifdef LAMP_DEBUG
    } else if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      const uint8_t* m = info.wifi_ap_staconnected.mac;
      Serial.printf("[wifi] AP station joined %02X:%02X:%02X:%02X:%02X:%02X\n",
                    m[0], m[1], m[2], m[3], m[4], m[5]);
    } else if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
      Serial.printf("[wifi] AP station got IP %s\n",
                    IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      Serial.println("[wifi] AP station left");
#endif
    }
  });

  // Kick the initial association from whatever WispConfig has cached.
  // If no creds are saved yet (fresh wisp) the helper logs and returns.
  reconnect();
}

void WifiLink::reconnect() {
  if (!config_) {
    Serial.println("[wifi] reconnect called before begin(); ignoring");
    return;
  }
  const std::string s = ssid();
  const std::string p = password();
  if (s.empty() || p.empty()) {
    Serial.println("[wifi] no creds; staying disconnected (radio stays up for ESP-NOW)");
    // disconnect(false) drops the association but keeps the WiFi radio up.
    // disconnect(true) turns the radio OFF, which kills ESP-NOW
    // (esp_now_send fails synchronously). The STA radio interface must
    // stay up so mesh broadcasts continue to land.
    WiFi.disconnect(false);
    return;
  }
  Serial.printf("[wifi] reconnect requested ssid=%s\n", s.c_str());
  // disconnect(false, false) drops the association without erasing the
  // stored creds inside the WiFi driver AND without turning the radio off.
  // We follow with a begin() to re-associate using whatever WispConfig
  // now holds.
  WiFi.disconnect(false, false);
  WiFi.begin(s.c_str(), p.c_str());
}

void WifiLink::startSoftAp(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_AP_STA);
  apUp_ = WiFi.softAP(ssid, pass, LAMP_ESPNOW_CHANNEL);
  mode_ = Mode::Ap;
  if (apUp_) {
    Serial.printf("[wifi] softAP up ssid=%s ch=%d ip=%s\n", ssid,
                  LAMP_ESPNOW_CHANNEL, WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("[wifi] softAP start FAILED ssid=%s\n", ssid);
  }
}

bool WifiLink::canBroadcast() const {
  if (mode_ == Mode::Ap) return apUp_;
  if (mode_ == Mode::Sta) return WiFi.status() == WL_CONNECTED;
  return false;
}

size_t WifiLink::apClientIps(IPAddress* out, size_t maxOut) const {
  if (mode_ != Mode::Ap) return 0;
  // ponytail: softAP DHCP leases run sequentially from .2, so derive client
  // IPs from the station count. A lease gap after disconnect/reconnect churn
  // would misaddress a lamp; swap for a real lease enumeration if that bites.
  const uint8_t num = WiFi.softAPgetStationNum();
  const IPAddress base = WiFi.softAPIP();
  size_t count = 0;
  for (uint8_t i = 0; i < num && count < maxOut; ++i) {
    out[count++] = IPAddress(base[0], base[1], base[2], 2 + i);
  }
  return count;
}

bool WifiLink::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

std::string WifiLink::ssid() const {
  if (!config_) return {};
  return std::string(config_->wifiSsid().c_str());
}

std::string WifiLink::password() const {
  if (!config_) return {};
  return std::string(config_->wifiPw().c_str());
}

}  // namespace wisp
