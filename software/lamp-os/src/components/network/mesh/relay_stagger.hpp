#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <lampos/clock.hpp>
#include <lampos/protocol/dedup_ring.hpp>

#include "relay_delay.hpp"

namespace lamp {

// A staggered relay goes out 0 to this many ms after first sight. Two copies
// started in it overlap with odds of about 2 x airtime / window. Longer delays
// wisp frames for lamps one hop out; shorter lets relayers collide.
constexpr uint32_t kRelayStaggerWindowMs = 150;

// A queued relay still unsent this long after its fire time is dropped. Under
// the wisp's 2 s status cadence, so a late copy never lands after the next
// status and rolls a lamp's cached status back.
constexpr uint32_t kMaxRelayLateMs = 1000;

#ifdef LAMP_DEBUG
struct RelayStaggerWindow {
  uint32_t queued = 0;
  uint32_t failOpen = 0;
  // Passes a due relay waited for want of a TX credit.
  uint32_t deferred = 0;
  uint32_t dropped = 0;
  uint32_t maxLateMs = 0;
};
#endif

// Delays each relay of a first-seen frame by relayDelayMs over
// [0, kRelayStaggerWindowMs], so the lamps that heard one frame do not all
// rebroadcast it in the same instant. Every enqueued frame goes out once,
// byte-identical, unless it is still unsent kMaxRelayLateMs after its fire
// time. enqueue runs on the recv task, service on the loop task. See
// docs/dev/networking.md.
template <size_t BufMax, size_t Slots>
class RelayStagger {
 public:
  // Set before the first enqueue that should be desynchronized.
  void begin(const uint8_t ownMac[6]) {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    std::memcpy(ownMac_, ownMac, 6);
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
  }

  // False when the table is full or the frame is over BufMax: the caller
  // relays it now.
  bool enqueue(const uint8_t* frame, size_t len, uint8_t msgType,
               const uint8_t sourceMac[6], uint16_t seq, uint32_t nowMs) {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    Slot* slot = nullptr;
    if (len <= BufMax) {
      for (Slot& s : slots_) {
        if (!s.used) { slot = &s; break; }
      }
    }
    if (slot) {
      slot->used = true;
      slot->fireAtMs = nowMs + relayDelayMs(ownMac_, sourceMac, msgType, seq,
                                            0, kRelayStaggerWindowMs);
      slot->len = static_cast<uint16_t>(len);
      std::memcpy(slot->frame, frame, len);
    }
#ifdef LAMP_DEBUG
    if (slot) {
      window_.queued++;
    } else {
      window_.failOpen++;
    }
#endif
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    return slot != nullptr;
  }

  // Sends each due relay while ready(nowMs) holds; without a credit it waits
  // for a later pass. send(frame, len) returning false is a driver error,
  // retried next pass.
  template <class ReadyFn, class SendFn>
  void service(uint32_t nowMs, ReadyFn&& ready, SendFn&& send) {
    for (Slot& s : slots_) {
      LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
      const bool due = s.used && lampos::timeReached(nowMs, s.fireAtMs);
      const uint32_t fireAtMs = s.fireAtMs;
      const uint16_t len = s.len;
      const bool late =
          due && lampos::timeReached(nowMs, fireAtMs + kMaxRelayLateMs);
      if (late) {
        s.used = false;
#ifdef LAMP_DEBUG
        window_.dropped++;
#endif
      }
      LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
      if (!due || late) continue;
      if (!ready(nowMs)) {
#ifdef LAMP_DEBUG
        LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
        window_.deferred++;
        LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
#endif
        continue;
      }
      // The slot stays used while it sends, so the recv task never writes it.
      if (!send(s.frame, len)) continue;
      LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
      s.used = false;
#ifdef LAMP_DEBUG
      const uint32_t lateMs = nowMs - fireAtMs;
      if (lateMs > window_.maxLateMs) window_.maxLateMs = lateMs;
#endif
      LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    }
  }

#ifdef LAMP_DEBUG
  RelayStaggerWindow window() const {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    const RelayStaggerWindow w = window_;
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    return w;
  }
  void resetWindow() {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    window_ = {};
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
  }
#endif

 private:
  struct Slot {
    bool used = false;
    uint32_t fireAtMs = 0;
    uint16_t len = 0;
    uint8_t frame[BufMax] = {0};
  };

  Slot slots_[Slots];
  uint8_t ownMac_[6] = {0};
  mutable LAMP_PROTOCOL_PORTMUX_TYPE mux_ = LAMP_PROTOCOL_PORTMUX_INIT;
#ifdef LAMP_DEBUG
  RelayStaggerWindow window_;
#endif
};

}  // namespace lamp
