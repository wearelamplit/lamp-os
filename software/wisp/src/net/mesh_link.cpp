#include "net/mesh_link.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstring>

namespace wisp {

MeshLink* MeshLink::s_instance = nullptr;

// ESP-NOW v2 max payload is 1470 B; reject anything outside [0, 1470]
// so a driver error can't cast to a huge size_t and reach handler_ with a
// bogus length.
static constexpr int kMaxRecvFrameLen = ESP_NOW_MAX_DATA_LEN_V2;

static void recvTrampoline(const esp_now_recv_info_t* info,
                           const uint8_t* data, int len) {
  if (!MeshLink::s_instance || !info) return;
  if (len < 0 || len > kMaxRecvFrameLen) return;
  auto& handler = MeshLink::s_instance->handler_;
  if (!handler) return;
  // ESP-NOW recv-info carries the rx_ctrl pointer when the IDF supports
  // it (true on arduino-esp32 v3.x / IDF 5.x). rx_ctrl->rssi is a
  // signed 8-bit bit-field on the C6. Fall back to INT8_MIN ("not
  // measured") if rx_ctrl is null. The WispRoster claim logic treats
  // that as "don't consider this lamp closer".
  int8_t rssi = INT8_MIN;
  if (info->rx_ctrl) {
    rssi = static_cast<int8_t>(info->rx_ctrl->rssi);
  }
  handler(info->src_addr, data, static_cast<size_t>(len), rssi);
}

#ifdef LAMP_DEBUG
// arduino-esp32 v3.x / C6 send callback; MAC in info->des_addr. FAIL means
// no MAC-layer ACK within the hardware retries; esp_now_send's return only
// reports the enqueue.
static void sendTrampoline(const wifi_tx_info_t* info,
                           esp_now_send_status_t status) {
  if (!info || status == ESP_NOW_SEND_SUCCESS) return;
  const uint8_t* mac = info->des_addr;
  Serial.printf("[mesh.send] FAIL to %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
#endif

bool MeshLink::begin() {
  s_instance = this;

  // Order matters: STA mode → disconnect from any AP but KEEP RADIO ON →
  // pin channel → init ESP-NOW. ESP-NOW only works while the WiFi radio
  // is powered. (false, false): keep stored creds and radio up.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.setSleep(false);

  // Xiao ESP32-C6 external antenna select. The arduino-esp32 v3.x C6
  // variant declares `static const uint8_t WIFI_ANT_CONFIG = 14;` as a
  // C++ constant, NOT a #define, so #ifdef misses it. Check the board
  // macro directly. ARDUINO_XIAO_ESP32C6 comes from the variant boards.txt.
  // Without this the wisp falls back to the internal antenna and signal
  // is too weak to reach lamps in another room.
#if defined(ARDUINO_XIAO_ESP32C6) || defined(WIFI_ANT_CONFIG)
  pinMode(WIFI_ANT_CONFIG, OUTPUT);
  digitalWrite(WIFI_ANT_CONFIG, HIGH);
#endif

  esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[mesh] esp_now_init failed");
    return false;
  }

  esp_now_register_recv_cb(recvTrampoline);
#ifdef LAMP_DEBUG
  esp_now_register_send_cb(sendTrampoline);
#endif

  // Every peer sends at the IDF's ESP-NOW default, 1 Mbps DSSS, the 802.11
  // rate that carries farthest. Far lamps already receive near -94 dBm, so
  // nothing here sets a faster one.

  // Broadcast peer. channel=0 means "use the radio's current channel", just
  // pinned above so this lands on LAMP_ESPNOW_CHANNEL.
  esp_now_peer_info_t peer = {};
  std::memset(&peer, 0, sizeof(peer));
  std::memset(peer.peer_addr, 0xFF, 6);
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[mesh] add_peer(broadcast) failed");
    return false;
  }

  uint8_t mac[6];
  getMac(mac);
  Serial.printf("[mesh] ready ch=%d mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
                LAMP_ESPNOW_CHANNEL, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return true;
}

void MeshLink::onPacket(MeshRecvFn handler) {
  handler_ = std::move(handler);
}

bool MeshLink::broadcast(const uint8_t* data, size_t len) {
  static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return esp_now_send(bcast, data, len) == ESP_OK;
}

bool MeshLink::send(const uint8_t targetMac[6], const uint8_t* data, size_t len) {
  if (!targetMac || !data) return false;

  // Decide under the mux; the ESP-NOW calls can't run inside it.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&peerMux_);
#endif
  const PeerSlots::Change change = peers_.touch(targetMac);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&peerMux_);
#endif

  if (change.evicted) {
    const uint8_t* old = change.evictedMac;
    esp_now_del_peer(old);
#ifdef LAMP_DEBUG
    Serial.printf("[mesh] LRU evict %02X:%02X:%02X:%02X:%02X:%02X\n",
                  old[0], old[1], old[2], old[3], old[4], old[5]);
#endif
  }
  if (change.added) {
    esp_now_peer_info_t peer = {};
    std::memset(&peer, 0, sizeof(peer));
    std::memcpy(peer.peer_addr, targetMac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
      portENTER_CRITICAL(&peerMux_);
#endif
      peers_.release(targetMac);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
      portEXIT_CRITICAL(&peerMux_);
#endif
      Serial.printf("[mesh] add_peer err=0x%x for %02X:%02X:%02X:%02X:%02X:%02X\n",
                    (unsigned)err,
                    targetMac[0], targetMac[1], targetMac[2],
                    targetMac[3], targetMac[4], targetMac[5]);
      return false;
    }
  }
  return esp_now_send(targetMac, data, len) == ESP_OK;
}

void MeshLink::getMac(uint8_t out[6]) const {
  esp_wifi_get_mac(WIFI_IF_STA, out);
}

}  // namespace wisp
