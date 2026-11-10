// Pure paint-scheduling math: drift slot cadence, rotation and fade, and the
// unicast gap gate. No Arduino deps so it unit-tests in the native env.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

// Slot between per-lamp drift sends: one lamp advances each slot, so a full
// sweep of N lamps takes intervalMs. 0 when there are no lamps.
inline uint32_t driftSlotMs(uint32_t intervalMs, size_t n) {
  return n ? intervalMs / static_cast<uint32_t>(n) : 0;
}

// Next rotation cursor, wrapping at n. 0 when n==0.
inline size_t nextDriftIdx(size_t idx, size_t n) {
  return n ? (idx + 1) % n : 0;
}

// Fade length of one drift step, drawn from `rnd`: within [20000, fadeMax]
// where fadeMax = 20000 + (intervalMs - 20000) * fadePct / 100.
inline uint32_t driftFadeMs(uint32_t intervalMs, uint8_t fadePct, uint32_t rnd) {
  const uint32_t lo = 20000;
  const uint32_t hi = intervalMs > lo
      ? lo + static_cast<uint32_t>((uint64_t)(intervalMs - lo) * fadePct / 100)
      : lo;
  const uint32_t span = hi - lo + 1;
  return lo + (rnd % span);
}

// Linear 0..255 progress `elapsedMs` into a `fadeMs` drift fade; 255 once the
// fade is done, and for fadeMs 0.
inline uint8_t driftFadeWeight(uint32_t elapsedMs, uint32_t fadeMs) {
  if (elapsedMs >= fadeMs) return 255;
  return static_cast<uint8_t>(static_cast<uint64_t>(elapsedMs) * 255u / fadeMs);
}

// True once at least minGapMs has elapsed since the last unicast send, any
// walk. Widens the real inter-send spacing past each walk's own per-peer
// pace so a no-ACK frame has room to ACK before the next goes out.
inline bool unicastGateOpen(uint32_t nowMs, uint32_t lastUnicastMs, uint32_t minGapMs) {
  return (nowMs - lastUnicastMs) >= minGapMs;
}

}  // namespace wisp
