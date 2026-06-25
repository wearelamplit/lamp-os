#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#endif

namespace wisp {

// ESP-NOW channel the lamp grid sits on. Matches LAMP_ESPNOW_CHANNEL in
// software/lamp-os/src/components/network/wifi.cpp. If the lamp side ever
// moves, mirror here.
#ifndef LAMP_ESPNOW_CHANNEL
// Moved 1 → 11 on 2026-06-10 to dodge 2.4 GHz channel-1 congestion
// (consumer-router default). Channel 11 is consistently the least-
// utilized of the three non-overlapping NA channels (1/6/11). Mixed-
// fleet across channels does NOT interoperate — peers on the old
// channel are invisible.
#define LAMP_ESPNOW_CHANNEL 11
#endif

// Handler signature for inbound frames. Fires from the WiFi recv task —
// no heap, no blocking, no logging. Copy bytes out and process on loop.
// `rssi` is the signed ESP-NOW RX RSSI for this frame, or INT8_MIN if
// the platform/test rig couldn't surface a measurement.
using MeshRecvFn = std::function<void(const uint8_t srcMac[6],
                                      const uint8_t* data, size_t len,
                                      int8_t rssi)>;

/**
 * @brief Thin ESP-NOW wrapper for wisp.
 *
 * - `begin()` claims the radio channel via `esp_wifi_set_channel`, brings up
 *   ESP-NOW, registers a single broadcast peer (FF:FF:FF:FF:FF:FF), and routes
 *   incoming frames to the registered handler.
 * - `send(...)` is a unicast helper (paint, OTA chunks).
 * - `broadcast(...)` drives HELLO + paint-broadcast paths.
 *
 * Caller is responsible for putting WiFi in STA mode + disconnecting any AP
 * association BEFORE calling begin(); MeshLink only pins the channel and
 * starts ESP-NOW.
 */
class MeshLink {
 public:
  bool begin();

  // Register the inbound handler. May be called before or after begin();
  // the trampoline checks the handler is set before dispatching.
  void onPacket(MeshRecvFn handler);

  // Broadcast to FF:FF:FF:FF:FF:FF. Returns true on send queued.
  bool broadcast(const uint8_t* data, size_t len);

  // Unicast to a specific peer. Adds the peer on first use (idempotent).
  // `msgType` is informational only — the payload is already a fully formed
  // lamp_protocol frame.
  bool send(const uint8_t targetMac[6], const uint8_t* data, size_t len);

  // Wisp's STA MAC. Useful for log lines + future addressed frames.
  void getMac(uint8_t out[6]) const;

  // ESP-NOW C callback can't capture; we store the std::function here and
  // dispatch through a static trampoline that reaches in via s_instance.
  // These two are public so the trampoline in the .cpp doesn't need a
  // friend declaration — they're an internal seam, not a sanctioned API.
  static MeshLink* s_instance;
  MeshRecvFn handler_;

 private:

  // Track whether we've already added a unicast peer so add_peer doesn't
  // return DUP, and so the table can evict the LRU peer when full.
  //
  // ESP-NOW caps the total peer table at ESP_NOW_MAX_TOTAL_PEER_NUM (20
  // on arduino-esp32 v3.x / IDF 5.x for the C6). MeshLink::begin()
  // registers the FF:FF:FF:FF:FF:FF broadcast peer in that table, so 19
  // unicast slots remain. With 22 lamps in the production fleet (and
  // headroom for growth), the table WILL fill — we evict the
  // least-recently-used unicast peer on overflow so newly seen MACs
  // continue to work.
  //
  // Before this fix the table was 16 slots with no eviction; once full,
  // a 17th MAC silently fell through with no esp_now_add_peer call and
  // every subsequent esp_now_send to that MAC returned
  // ESP_ERR_ESPNOW_NOT_FOUND.
  static constexpr size_t MAX_TRACKED_PEERS = 19;
  struct PeerSlot {
    uint8_t  mac[6];
    uint32_t lastUsedMs;  // millis() of most recent send to this peer.
    bool     used;
  };
  PeerSlot peers_[MAX_TRACKED_PEERS] = {};
  size_t   peerCount_ = 0;

  // Locate a tracked peer by MAC. Returns the slot index or -1.
  // Caller must hold peerMux_.
  int findPeerSlot(const uint8_t mac[6]) const;
  // Pick the slot with the smallest lastUsedMs. Used on overflow.
  // Caller must hold peerMux_.
  size_t pickLruSlot() const;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Mutex around the peer table. send() is called from the loop task
  // (PaintDistributor / OFFER from tick()); without a mutex two
  // concurrent first-touches of the same NEW MAC could both pass the
  // "not present" check, both call
  // esp_now_add_peer (the second returns ESP_ERR_ESPNOW_EXIST, benign),
  // and both write into peers_ at peerCount_++ (NOT benign — torn
  // increment, duplicate slot, eventual eviction picking the wrong
  // entry). A portMUX critical section is the right primitive here —
  // mutations are tiny and we never block while holding it.
  mutable portMUX_TYPE peerMux_ = portMUX_INITIALIZER_UNLOCKED;
#endif
};

}  // namespace wisp
