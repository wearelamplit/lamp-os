#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lamp {

// A bench sees at most a handful of wisps in range, typically 1; 8 slots
// give headroom over that without a heap allocation.
constexpr size_t kWispCoexMaxPeers = 8;

// A seq delta above this is a wisp reboot (SeqSource restarts at 0), not
// broadcast loss; treated as a resync rather than a loss spike.
constexpr uint16_t kWispCoexResyncThreshold = 1024;

// Missed-hello count between two seq observations, uint16-wraparound-safe.
// Duplicate/retransmit (delta 0) and an implausible jump (delta above
// kWispCoexResyncThreshold, a wisp reboot) both count as 0 missed; the
// caller re-baselines lastSeq to newSeq either way.
inline uint16_t wispCoexMissed(uint16_t lastSeq, uint16_t newSeq) {
  const uint16_t delta = static_cast<uint16_t>(newSeq - lastSeq);
  if (delta == 0 || delta > kWispCoexResyncThreshold) return 0;
  return static_cast<uint16_t>(delta - 1);
}

struct WispCoexSlot {
  uint8_t mac[6] = {0};
  bool used = false;
  bool haveSeq = false;
  uint16_t lastSeq = 0;
  uint32_t recv = 0;
  uint32_t missed = 0;
  uint32_t lastHelloMs = 0;
  uint32_t maxGapMs = 0;
  uint32_t lastEmitMs = 0;
};

// Per-wisp seq-gap accumulator: fixed slot array keyed by MAC, linear
// scan, no heap (docs/dev/embedded-heap.md). Not thread-safe; the caller
// (handleRecv) runs single-threaded on the ESP-NOW recv task.
class WispCoexMeter {
 public:
  WispCoexSlot& record(const uint8_t mac[6], uint16_t seq, uint32_t nowMs) {
    WispCoexSlot& slot = findOrCreate(mac);
    if (slot.haveSeq) {
      slot.missed += wispCoexMissed(slot.lastSeq, seq);
      const uint32_t gap = nowMs - slot.lastHelloMs;
      if (gap > slot.maxGapMs) slot.maxGapMs = gap;
    }
    slot.lastSeq = seq;
    slot.haveSeq = true;
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
