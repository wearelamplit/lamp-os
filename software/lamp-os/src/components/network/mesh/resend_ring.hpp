#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lamp {

// Spaced re-broadcast buffer for an unacked ESP-NOW frame: an unacked
// broadcast has no per-frame retry, so the same frame (same seq) is re-emitted
// `resends` extra times, `gapMs` apart, and the receiver's DedupRing collapses
// the copies to one apply. Spacing decorrelates the copies against the peer's
// scan-listen gaps so they don't all land in the same hole. `Count` slots let
// one fan-out (a cascade addressing many peers) keep every target's frame in
// flight at once; a frame longer than `BufMax` is rejected and its single send
// stands. Receive-side state, no wire contract. See docs/dev/networking.md.
template <size_t BufMax, size_t Count>
class ResendRing {
 public:
  bool enqueue(const uint8_t* frame, size_t len, uint32_t nowMs,
               uint8_t resends, uint32_t gapMs) {
    if (len == 0 || len > BufMax) return false;
    Slot& s = slots_[cursor_];
    cursor_ = (cursor_ + 1) % Count;
    std::memcpy(s.buf, frame, len);
    s.len = static_cast<uint16_t>(len);
    s.remaining = resends;
    s.gapMs = gapMs;
    s.nextMs = nowMs + gapMs;
    return true;
  }

  // Re-emit every slot whose gap has elapsed, one copy per due slot per call.
  // sendFn(buf, len) performs the broadcast.
  template <typename SendFn>
  void service(uint32_t nowMs, SendFn&& sendFn) {
    for (Slot& s : slots_) {
      if (s.remaining == 0) continue;
      if (static_cast<int32_t>(nowMs - s.nextMs) < 0) continue;
      sendFn(s.buf, s.len);
      --s.remaining;
      s.nextMs += s.gapMs;
    }
  }

 private:
  struct Slot {
    uint8_t buf[BufMax];
    uint16_t len = 0;
    uint8_t remaining = 0;
    uint32_t gapMs = 0;
    uint32_t nextMs = 0;
  };
  Slot slots_[Count]{};
  size_t cursor_ = 0;
};

}  // namespace lamp
