#include "net/mesh_link.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstring>

namespace wisp {

MeshLink* MeshLink::s_instance = nullptr;

// ESP-NOW max payload per spec is 250 B; reject anything outside [0, 250]
// so a driver error can't cast to a huge size_t and reach handler_ with a
// bogus length.
static constexpr int kMaxRecvFrameLen = 250;

static void recvTrampoline(const esp_now_recv_info_t* info,
                           const uint8_t* data, int len) {
  if (!MeshLink::s_instance || !info) return;
  if (len < 0 || len > kMaxRecvFrameLen) return;
  auto& handler = MeshLink::s_instance->handler_;
  if (!handler) return;
  // ESP-NOW recv-info carries the rx_ctrl pointer when the IDF supports
  // it (true on arduino-esp32 v3.x / IDF 5.x). rx_ctrl->rssi is a
  // signed 8-bit bit-field on the C6. Fall back to INT8_MIN ("not
  // measured") if rx_ctrl is null — the WispRoster claim logic treats
  // that as "don't consider this lamp closer".
  int8_t rssi = INT8_MIN;
  if (info->rx_ctrl) {
    rssi = static_cast<int8_t>(info->rx_ctrl->rssi);
  }
  handler(info->src_addr, data, static_cast<size_t>(len), rssi);
}

// ESP-NOW send-complete callback. Fires from the WiFi task with the
// MAC-layer delivery result: ESP_NOW_SEND_SUCCESS = peer ACKed; FAIL =
// no ACK received within hardware retries. Without this hook a
// silently-dropped unicast looks like ESP_OK at the call site (the
// IDF only reports the enqueue result).
// IDF 5.x on the C6 widened the callback signature from `(const uint8_t*,
// status)` to `(const wifi_tx_info_t*, status)`. The MAC now lives in
// `info->des_addr`. This matches the typedef `esp_now_send_cb_t` in
// `esp_now.h` for arduino-esp32 v3.x on the C6.
static void sendTrampoline(const wifi_tx_info_t* info,
                           esp_now_send_status_t status) {
  if (!MeshLink::s_instance || !info) return;
  if (status == ESP_NOW_SEND_SUCCESS) return;
  const uint8_t* mac = info->des_addr;
  Serial.printf("[mesh.send] FAIL to %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool MeshLink::begin() {
  s_instance = this;

  // Order matters: STA mode → disconnect from any AP but KEEP RADIO ON →
  // pin channel → init ESP-NOW. ESP-NOW only works while the WiFi radio
  // is powered. Calling WiFi.disconnect(true, true) was a bug — the
  // second arg is `wifioff` and turning it on shuts the radio down,
  // which silently broke recv (no frames seen, MAC reads as 00:00:..).
  // Use disconnect(false, false) — clear the AP context, keep radio up.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.setSleep(false);

  // Xiao ESP32-C6 external antenna select. The arduino-esp32 v3.x C6
  // variant declares `static const uint8_t WIFI_ANT_CONFIG = 14;` as a
  // C++ constant, NOT a #define — so #ifdef misses it. Check the board
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
  esp_now_register_send_cb(sendTrampoline);

  // Broadcast peer. channel=0 means "use the radio's current channel" — we
  // just pinned it above so this lands on LAMP_ESPNOW_CHANNEL.
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

int MeshLink::findPeerSlot(const uint8_t mac[6]) const {
  for (size_t i = 0; i < MAX_TRACKED_PEERS; ++i) {
    if (peers_[i].used && std::memcmp(peers_[i].mac, mac, 6) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

size_t MeshLink::pickLruSlot() const {
  // Find the slot with the smallest lastUsedMs among USED entries.
  // Assumes the caller has already verified the table is full
  // (peerCount_ == MAX_TRACKED_PEERS), so at least one used slot
  // exists.
  size_t bestIdx = 0;
  uint32_t bestMs = UINT32_MAX;
  for (size_t i = 0; i < MAX_TRACKED_PEERS; ++i) {
    if (peers_[i].used && peers_[i].lastUsedMs < bestMs) {
      bestMs = peers_[i].lastUsedMs;
      bestIdx = i;
    }
  }
  return bestIdx;
}

bool MeshLink::send(const uint8_t targetMac[6], const uint8_t* data, size_t len) {
  if (!targetMac || !data) return false;

  // Bookkeeping path: ensure the target is in our local table AND in
  // ESP-NOW's peer table. Touch lastUsedMs on hit; on miss, insert
  // (allocating an empty slot or evicting the LRU). Two-stage so the
  // critical section under peerMux_ stays free of ESP-NOW calls (which
  // can return ESP_ERR_ESPNOW_NO_MEM / take internal locks) — we
  // capture the decision under mux, then run esp_now_add_peer /
  // esp_now_del_peer OUTSIDE the mux.
  uint8_t  macToDel[6] = {0};
  bool     needDel = false;
  bool     needAdd = false;

  const uint32_t nowMs = millis();
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&peerMux_);
#endif
  int existing = findPeerSlot(targetMac);
  if (existing >= 0) {
    peers_[existing].lastUsedMs = nowMs;
  } else if (peerCount_ < MAX_TRACKED_PEERS) {
    // Find the first empty slot. Linear scan is fine — table is small.
    for (size_t i = 0; i < MAX_TRACKED_PEERS; ++i) {
      if (!peers_[i].used) {
        peers_[i].used = true;
        std::memcpy(peers_[i].mac, targetMac, 6);
        peers_[i].lastUsedMs = nowMs;
        peerCount_++;
        needAdd = true;
        break;
      }
    }
  } else {
    // Full → evict LRU. Copy the old MAC out for esp_now_del_peer; we
    // can't call IDF APIs under the critical section.
    const size_t lru = pickLruSlot();
    std::memcpy(macToDel, peers_[lru].mac, 6);
    needDel = true;
    std::memcpy(peers_[lru].mac, targetMac, 6);
    peers_[lru].lastUsedMs = nowMs;
    needAdd = true;
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&peerMux_);
#endif

  if (needDel) {
    esp_now_del_peer(macToDel);
    // Evictions are expected at steady-state in a busy crowd.
    Serial.printf("[mesh] LRU evict %02X:%02X:%02X:%02X:%02X:%02X\n",
                  macToDel[0], macToDel[1], macToDel[2],
                  macToDel[3], macToDel[4], macToDel[5]);
  }
  if (needAdd) {
    esp_now_peer_info_t peer = {};
    std::memset(&peer, 0, sizeof(peer));
    std::memcpy(peer.peer_addr, targetMac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
      // Tracking-table is committed (we already wrote into peers_) but
      // the ESP-NOW side rejected the add. esp_now_send will return
      // NOT_FOUND. Surface this to caller so the distributor doesn't
      // mistake it for a queue-full transient.
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
