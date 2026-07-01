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

// Map a raw 32-bit random to the drift fade range [6000, 23000] ms.
inline uint16_t rollDriftFadeMs(uint32_t rnd) {
  constexpr uint16_t kLo = 6000, kHi = 23000;
  constexpr uint32_t kSpan = static_cast<uint32_t>(kHi - kLo) + 1;
  return static_cast<uint16_t>(kLo + (rnd % kSpan));
}

}  // namespace wisp
