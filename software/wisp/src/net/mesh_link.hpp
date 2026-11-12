#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "net/peer_slots.hpp"

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

  // Unicast a fully formed lamp_protocol frame, registering the peer with
  // ESP-NOW on first use. False when the frame was not queued.
  bool send(const uint8_t targetMac[6], const uint8_t* data, size_t len);

  // Wisp's STA MAC, for log lines.
  void getMac(uint8_t out[6]) const;

  // Trampoline seam: ESP-NOW C callback can't capture; reaches handler_ via s_instance.
  static MeshLink* s_instance;
  MeshRecvFn handler_;

 private:

  PeerSlots peers_;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Guards peers_: two sends racing on one new MAC could both miss it and
  // take two slots. portMUX fits: tiny, non-blocking mutations.
  mutable portMUX_TYPE peerMux_ = portMUX_INITIALIZER_UNLOCKED;
#endif
};

}  // namespace wisp
