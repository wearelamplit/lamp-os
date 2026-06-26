#pragma once

#include <cstdint>
#include <cstddef>

namespace lamp {

// Callback signature for received ESP-NOW frames. Fires from the Wi-Fi task —
// DO NOT do heap work or block. Copy the bytes into a queue and process in loop().
//
// `rssi` is the per-frame RSSI in dBm pulled from `recv_info->rx_ctrl->rssi`
// (ESP-IDF 5.x). Receivers feed it into NearbyLamp::lastRssi so the cascade
// path can sort peers by signal strength (≈ physical proximity) to produce
// an outward spatial wave on triggering. -127 indicates "unknown".
using EspNowRecvFn = void (*)(const uint8_t* mac, const uint8_t* data, size_t len,
                              int8_t rssi);

class EspNowLink {
 public:
  // Init ESP-NOW, register the broadcast peer, and route incoming frames
  // to `recv`. The radio channel is owned by the wifi module (peer.channel=0
  // tracks the current channel). Returns true on success.
  bool begin(EspNowRecvFn recv);

  // Broadcast to FF:FF:FF:FF:FF:FF. Returns true if send queued.
  bool broadcast(const uint8_t* data, size_t len);

  // Populate `out` (6 bytes) with this device's Wi-Fi STA MAC. Caller must
  // ensure begin() has run.
  void getMac(uint8_t out[6]);

  // Public so the C trampoline in the .cpp can reach it without a friend.
  static EspNowRecvFn s_recv;
};

}  // namespace lamp
