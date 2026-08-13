#pragma once

#include <cstdint>
#include <cstring>

// portMUX is FreeRTOS-only. The header is also compiled by native unit tests;
// guard the include so a native compile of this header builds, and provide a
// no-op fallback.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <portmacro.h>
#define LAMP_PROTOCOL_PORTMUX_TYPE        portMUX_TYPE
#define LAMP_PROTOCOL_PORTMUX_INIT        portMUX_INITIALIZER_UNLOCKED
#define LAMP_PROTOCOL_PORTMUX_ENTER(mux)  portENTER_CRITICAL(mux)
#define LAMP_PROTOCOL_PORTMUX_EXIT(mux)   portEXIT_CRITICAL(mux)
#else
struct LampProtocolNullMux {};
#define LAMP_PROTOCOL_PORTMUX_TYPE        LampProtocolNullMux
#define LAMP_PROTOCOL_PORTMUX_INIT        {}
#define LAMP_PROTOCOL_PORTMUX_ENTER(mux)  ((void)(mux))
#define LAMP_PROTOCOL_PORTMUX_EXIT(mux)   ((void)(mux))
#endif

// DedupRing, the gossip-relay duplicate suppressor. Not a wire message; the
// receive-side state that makes gossip relay terminate. It keys on the
// (sourceMac, msgType, seq) tuple carried in
// every frame header (bytes 3..5 + the src MAC each family places at byte 6),
// holding the last CAPACITY tuples seen. record() returns
// false for a tuple already in the ring (drop the relay) and true for a fresh
// one (relay it once). Carries the FreeRTOS portMUX shim it needs, since the
// critical section runs from both the ESP-NOW recv task and the loop task.

namespace lamp_protocol {

// Gossip dedup: small fixed-size ring tracking (sourceMac, msgType, seq) tuples
// seen recently. Drops duplicates so re-broadcasts terminate.
//
// CAPACITY is per-instance receive-side state, sized to a message type's
// traffic; it is not a wire contract, so nodes with different capacities
// interoperate. Relay-heavy every-lamp types (HELLO, CONTROL_OP) want the
// full ring so a late-arriving relay in a dense crowd still hits dedup before
// the slot rotates; single-hop / low-rate types need far less.
//
// record() runs from both the ESP-NOW recv path and the send path (recording
// self-sent ops so the inbound re-broadcast doesn't loop back), which on the
// multi-core lamp are different tasks. The portMUX critical section around the
// compare loop + slot write is kept SHORT: no allocations, no network calls,
// no logging.
template <size_t CAPACITY = 64>
class DedupRing {
 public:
  // Returns true if (mac, msgType, seq) is new (and records it); false if seen.
  bool record(const uint8_t mac[6], uint8_t msgType, uint16_t seq) {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    for (size_t i = 0; i < CAPACITY; i++) {
      const Entry& e = entries_[i];
      if (e.used && e.msgType == msgType && e.seq == seq &&
          std::memcmp(e.mac, mac, 6) == 0) {
        LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
        return false;
      }
    }
    Entry& slot = entries_[head_];
    slot.used = true;
    slot.msgType = msgType;
    slot.seq = seq;
    std::memcpy(slot.mac, mac, 6);
    head_ = (head_ + 1) % CAPACITY;
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    return true;
  }

 private:
  struct Entry {
    bool used = false;
    uint8_t msgType = 0;
    uint16_t seq = 0;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  };
  Entry entries_[CAPACITY];
  size_t head_ = 0;
  LAMP_PROTOCOL_PORTMUX_TYPE mux_ = LAMP_PROTOCOL_PORTMUX_INIT;
};

}  // namespace lamp_protocol
