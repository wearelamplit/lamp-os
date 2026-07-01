// Pure drift math: slot cadence, rotation, fade roll. No Arduino deps so it
// unit-tests in the native env and both the engine and tests share one source.
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

// Fade duration within [20000, fadeMax] where fadeMax = 20000 + (intervalMs-20000)*fadePct/100.
inline uint32_t driftFadeMs(uint32_t intervalMs, uint8_t fadePct, uint32_t rnd) {
  const uint32_t lo = 20000;
  const uint32_t hi = intervalMs > lo
      ? lo + static_cast<uint32_t>((uint64_t)(intervalMs - lo) * fadePct / 100)
      : lo;
  const uint32_t span = hi - lo + 1;
  return lo + (rnd % span);
}

}  // namespace wisp
