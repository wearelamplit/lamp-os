#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#endif

namespace wisp {

// Must match the lamp firmware's ESP-NOW channel.
#ifndef LAMP_ESPNOW_CHANNEL
// All ESP-NOW peers must share this channel.
#define LAMP_ESPNOW_CHANNEL 6
#endif

// Handler signature for inbound frames. Fires from the WiFi recv task: no
// heap, no blocking, no logging; copy bytes out and process on loop. rssi is
// the ESP-NOW RX RSSI, or INT8_MIN if the platform couldn't measure it.
using MeshRecvFn = std::function<void(const uint8_t srcMac[6],
                                      const uint8_t* data, size_t len,
                                      int8_t rssi)>;

// Thin ESP-NOW wrapper. begin() pins the radio channel, starts ESP-NOW,
// registers the FF:FF:FF:FF:FF:FF broadcast peer, and routes inbound frames
// to the handler. send() unicasts; broadcast() drives HELLO + paint. Caller
// must put WiFi in STA mode and drop any AP association before begin().
class MeshLink {
 public:
  bool begin();

  // Register the inbound handler. May be called before or after begin();
  // the trampoline checks the handler is set before dispatching.
  void onPacket(MeshRecvFn handler);

  // Broadcast to FF:FF:FF:FF:FF:FF. Returns true on send queued.
  bool broadcast(const uint8_t* data, size_t len);

  // Unicast to a specific peer. Adds the peer on first use (idempotent).
  // `msgType` is informational only. The payload is already a fully formed
  // lamp_protocol frame.
  bool send(const uint8_t targetMac[6], const uint8_t* data, size_t len);

  // Wisp's STA MAC, for log lines.
  void getMac(uint8_t out[6]) const;

  // Trampoline seam: ESP-NOW C callback can't capture; reaches handler_ via s_instance.
  static MeshLink* s_instance;
  MeshRecvFn handler_;

 private:

  // ESP-NOW caps the peer table at ESP_NOW_MAX_TOTAL_PEER_NUM (20 on the C6).
  // begin() registers the broadcast peer, leaving 19 unicast slots; with more
  // than 19 peers the LRU unicast peer is evicted on overflow to keep newly
  // seen MACs working.
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
  // Guards the peer table. send() runs on the loop task; without this, two
  // concurrent first-touches of the same new MAC could both pass the
  // "not present" check and both write peers_ at peerCount_++ (torn
  // increment, duplicate slot, wrong eviction). portMUX fits: tiny,
  // non-blocking mutations.
  mutable portMUX_TYPE peerMux_ = portMUX_INITIALIZER_UNLOCKED;
#endif
};

}  // namespace wisp
