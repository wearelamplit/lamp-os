#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include <lampos/protocol/dedup_ring.hpp>
#include <lampos/protocol/presence.hpp>

namespace lamp {

// Duplicates of a (mac, seq) that must be heard before its relay is
// suppressed. Too low starves genuine far edges of coverage; too high and
// suppression never engages, leaving HELLO airtime ~N^2.
constexpr uint8_t kHelloSuppressThreshold = 3;

// Randomized relay-delay window. The spread lets neighbors' duplicate counts
// accumulate before anyone fires. Too short and every node relays before
// hearing its neighbors (no suppression); too long and roster convergence lags.
constexpr uint32_t kHelloRelayJitterMinMs = 50;
constexpr uint32_t kHelloRelayJitterMaxMs = 200;

// Concurrent first-seen HELLOs tracked. Overflow fails open (relay now), so a
// too-small table only costs extra airtime under a burst, never coverage.
constexpr size_t kHelloPendingSlots = 16;

// Counter-based HELLO rebroadcast suppression. Receive-side only, no wire
// change. Instead of relaying every first-seen HELLO (airtime ~N^2), a
// first-seen frame is enqueued with a randomized fire delay; each duplicate
// (mac, seq) heard before it fires is one neighbor that already relayed it. At
// fire time an entry with >= kHelloSuppressThreshold duplicates is dropped (the
// mesh already covered it); otherwise the stored frame is relayed verbatim.
//
// Clock is injected: onFirstSeen/tick take nowMs so native tests drive a fake
// clock. Jitter is derived deterministically from (mac, seq), never rand().
class HelloRelaySuppressor {
 public:
  using RelayFn = std::function<void(const uint8_t* frame, size_t len)>;

  // Enqueue a first-seen HELLO. Returns true if the caller must relay it now
  // (table full or frame over cap, fail-open); false once enqueued.
  bool onFirstSeen(const uint8_t mac[6], uint16_t seq, const uint8_t* frame,
                   size_t len, uint32_t nowMs);

  // A duplicate (mac, seq) arrived; bump its coverage counter if still pending.
  void onDuplicate(const uint8_t mac[6], uint16_t seq);

  // Fire every entry whose delay elapsed: relay if under-covered, else drop.
  // Frees the slot either way.
  void tick(uint32_t nowMs, const RelayFn& relay);

  // Relay delay for a (mac, seq), spread across [min, max]. Public for the
  // native timing test to compute the exact fire boundary.
  static uint32_t jitterMs(const uint8_t mac[6], uint16_t seq);

#ifdef LAMP_DEBUG
  uint32_t suppressedWindow() const {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    const uint32_t v = suppressed_;
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    return v;
  }
  uint32_t relayedWindow() const {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    const uint32_t v = relayed_;
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    return v;
  }
  void resetWindow() {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    suppressed_ = 0;
    relayed_ = 0;
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
  }
#endif

 private:
  struct Pending {
    bool used = false;
    uint8_t mac[6] = {0};
    uint16_t seq = 0;
    uint8_t dupCount = 0;
    uint32_t fireAtMs = 0;
    uint16_t len = 0;
    uint8_t frame[lamp_protocol::HELLO_MAX_SIZE] = {0};
  };

  Pending* find(const uint8_t mac[6], uint16_t seq);

  Pending slots_[kHelloPendingSlots];
  // slots_ is written from the recv task (onFirstSeen/onDuplicate) and the loop
  // task (tick/resetWindow); guard every access.
  mutable LAMP_PROTOCOL_PORTMUX_TYPE mux_ = LAMP_PROTOCOL_PORTMUX_INIT;
#ifdef LAMP_DEBUG
  uint32_t suppressed_ = 0;
  uint32_t relayed_ = 0;
#endif
};

}  // namespace lamp
