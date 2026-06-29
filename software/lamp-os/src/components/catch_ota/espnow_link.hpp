#pragma once

#include <cstddef>
#include <cstdint>

namespace catch_ota {

// RecvFn is invoked on the loop task (via espnowPoll), never on the WiFi task.
// mac: sender's 6-byte MAC; data/len: raw ESP-NOW frame bytes.
using RecvFn = void (*)(const uint8_t mac[6], const uint8_t* data, size_t len);

// Init ESP-NOW, register the broadcast peer (FF:FF:FF:FF:FF:FF, channel=0),
// and store recv for dispatch by espnowPoll(). WiFi STA mode must already be
// up before calling. Returns true on success.
bool espnowBegin(RecvFn recv);

// Register a unicast peer MAC at channel=0 before sending unicast frames.
// No-op if the peer is already registered.
void espnowAddPeer(const uint8_t mac[6]);

// Re-init ESP-NOW after a WiFi mode switch (radioEnterOtaMode) clobbers the
// driver's peer table / interface binding. Restores the recv cb + broadcast
// peer; the caller re-adds unicast peers afterward.
void espnowReinit();

// Submit len bytes to mac via esp_now_send. Returns true if queued to driver.
// Call espnowAddPeer for any unicast target before the first send.
bool espnowSend(const uint8_t mac[6], const uint8_t* data, size_t len);

// Drain all frames the WiFi-task trampoline queued, invoking RecvFn for each.
// Loop task only. SPSC ring; see espnow_link.cpp for the release/acquire detail.
void espnowPoll();

}  // namespace catch_ota
