#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lamp {

// A bench sees at most a handful of wisps in range, typically 1; 8 slots
// give headroom over that without a heap allocation.
constexpr size_t kWispCoexMaxPeers = 8;

struct WispCoexSlot {
  uint8_t mac[6] = {0};
  bool used = false;
  uint32_t recv = 0;
  uint32_t lastHelloMs = 0;
  uint32_t maxGapMs = 0;
  uint32_t lastEmitMs = 0;
};

// Per-wisp presence tracker: fixed slot array keyed by MAC, linear scan, no
// heap (docs/dev/embedded-heap.md). No loss/seq-gap math: the wisp shares one
// seq counter across all its message types, so a per-type seq gap is not loss.
// maxGapMs (wall-clock time between hellos) is the real presence signal. Not
// thread-safe; the caller (handleRecv) runs single-threaded on the ESP-NOW
// recv task.
class WispCoexMeter {
 public:
  WispCoexSlot& record(const uint8_t mac[6], uint32_t nowMs) {
    WispCoexSlot& slot = findOrCreate(mac);
    if (slot.recv > 0) {
      const uint32_t gap = nowMs - slot.lastHelloMs;
      if (gap > slot.maxGapMs) slot.maxGapMs = gap;
    }
    slot.recv++;
    slot.lastHelloMs = nowMs;
    return slot;
  }

 private:
  WispCoexSlot slots_[kWispCoexMaxPeers];

  WispCoexSlot& findOrCreate(const uint8_t mac[6]) {
    for (auto& s : slots_) {
      if (s.used && std::memcmp(s.mac, mac, 6) == 0) return s;
    }
    for (auto& s : slots_) {
      if (!s.used) {
        s = WispCoexSlot();
        std::memcpy(s.mac, mac, 6);
        s.used = true;
        return s;
      }
    }
    // ponytail: kWispCoexMaxPeers ceiling; 9th distinct wisp recycles
    // slot 0 and drops its history. Widen kWispCoexMaxPeers if a bench
    // ever runs more concurrent wisps than that.
    WispCoexSlot& s = slots_[0];
    s = WispCoexSlot();
    std::memcpy(s.mac, mac, 6);
    s.used = true;
    return s;
  }
};

}  // namespace lamp
