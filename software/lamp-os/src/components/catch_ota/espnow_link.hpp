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

// Drain all frames queued by the WiFi-task trampoline and invoke RecvFn for
// each one. Must be called from the loop task only.
//
// SPSC contract: the WiFi-task trampoline is the sole producer — it writes
// frames into the ring and advances head. espnowPoll is the sole consumer —
// it reads frames and advances tail. No locking is required; std::atomic
// head/tail with release/acquire ordering enforce slot-body visibility across
// the two cores.
void espnowPoll();

}  // namespace catch_ota
