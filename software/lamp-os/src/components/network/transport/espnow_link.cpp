#include "espnow_link.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstring>

namespace lamp {

EspNowRecvFn EspNowLink::s_recv = nullptr;

// ESP-NOW v2 max payload is 1470 B; reject anything outside [0, 1470] so a
// negative/garbage len from a driver error path can't be cast to a huge size_t.
static constexpr int kMaxRecvFrameLen = ESP_NOW_MAX_DATA_LEN_V2;

static void recvTrampoline(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (EspNowLink::s_recv == nullptr || info == nullptr) return;
  if (len < 0 || len > kMaxRecvFrameLen) {
#ifdef LAMP_DEBUG
    Serial.printf("[espnow] reject len=%d\n", len);
#endif
    return;
  }
  // RSSI lives in the IDF rx_ctrl block. Older drivers / synthetic frames
  // can present a null rx_ctrl, so guard and default to -127 ("unknown").
  // -127 sorts to the back of the RSSI-desc list so a peer with no signal
  // info ends up firing last in the cascade.
  int8_t rssi = -127;
  if (info->rx_ctrl != nullptr) {
    rssi = static_cast<int8_t>(info->rx_ctrl->rssi);
  }
  EspNowLink::s_recv(info->src_addr, data, static_cast<size_t>(len), rssi);
}

// TX-completion diagnostic. Fires on the WiFi task after the radio either
// emits the frame or declines (queue full, driver error, no peer, etc.).
// Added 2026-06-03 to diagnose cascade reception silence — log the actual
// PHY result so we can tell whether broadcasts physically left the radio.
static void sendTrampoline(const esp_now_send_info_t* /*info*/,
                           esp_now_send_status_t status) {
#ifdef LAMP_DEBUG
  // TX diagnostic — only log failures. We already verified earlier in the
  // 2026-06-04 session that all sends succeed at PHY layer; logging every
  // OK floods serial and corrupts other logs. Keep the FAIL log so a real
  // TX failure still surfaces.
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("[espnow.tx] FAIL");
  }
#endif
}

bool EspNowLink::begin(EspNowRecvFn recv) {
  s_recv = recv;

  // WiFi STA mode is already up via wifi::begin() in standard_lamp setup;
  // do NOT call WiFi.mode/disconnect/setSleep here; that would clobber the
  // radio state the wifi module relies on for periodic presence scans.
  // Channel coordination is the wifi module's job; peer.channel=0 is
  // set below so the peer record tracks "whatever channel the
  // radio is on right now".

  if (esp_now_init() != ESP_OK) {
    Serial.println("[espnow] esp_now_init failed");
    return false;
  }

  esp_now_register_recv_cb(recvTrampoline);
  esp_now_register_send_cb(sendTrampoline);

  // 1M DSSS: the robust rate. Broadcasts (HELLO/cascade) get no MAC ACK/retry,
  // so robustness matters; coex is slot-scheduled, so a faster rate frees no
  // BLE airtime. TX-side only; receivers auto-detect from the PLCP header.
  esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);

  esp_now_peer_info_t peer = {};
  std::memset(&peer, 0, sizeof(peer));
  std::memset(peer.peer_addr, 0xFF, 6);
  // channel=0 means "current channel"; works whether the radio is on the
  // home AP's channel or the wifi module pinned it to LAMP_ESPNOW_CHANNEL.
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[espnow] esp_now_add_peer(broadcast) failed");
    return false;
  }

#ifdef LAMP_ESPNOW_TX_QDBM
  // Bench lossy-link simulation: cap TX power (0.25 dBm units, 8..84) to weaken
  // the ESP-NOW signal like distance/path-loss. Absent in shipping builds.
  esp_wifi_set_max_tx_power(LAMP_ESPNOW_TX_QDBM);
  Serial.printf("[espnow] bench TX power capped at %d (qdBm)\n", (int)LAMP_ESPNOW_TX_QDBM);
#endif

  return true;
}

bool EspNowLink::broadcast(const uint8_t* data, size_t len) {
  static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const esp_err_t err = esp_now_send(bcast, data, len);
#ifdef LAMP_DEBUG
  // esp_now_send returning non-ESP_OK means the frame never hits the driver
  // queue; the send callback never fires. Callers discard this return value.
  if (err != ESP_OK) {
    Serial.printf("[espnow.tx] submit FAIL err=%d (0x%x) len=%u\n",
                  (int)err, (unsigned)err, (unsigned)len);
  }
#endif
  return err == ESP_OK;
}

// ponytail: single cached peer slot, not an LRU table; one display wisp. Grow to N-slot only if we ever unicast to multiple wisps at once.
bool EspNowLink::send(const uint8_t mac[6], const uint8_t* data, size_t len) {
  if (mac == nullptr || data == nullptr) return false;
  if (peerCacheNeedsReadd(peerSet_, peerMac_, mac)) {
    if (peerSet_) esp_now_del_peer(peerMac_);
    esp_now_peer_info_t peer = {};
    std::memset(&peer, 0, sizeof(peer));
    std::memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    const esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
#ifdef LAMP_DEBUG
      Serial.printf("[espnow] add_peer err=%d (0x%x)\n", (int)err, (unsigned)err);
#endif
      return false;
    }
    std::memcpy(peerMac_, mac, 6);
    peerSet_ = true;
  }
  return esp_now_send(mac, data, len) == ESP_OK;
}

void EspNowLink::getMac(uint8_t out[6]) {
  esp_wifi_get_mac(WIFI_IF_STA, out);
}

}  // namespace lamp
