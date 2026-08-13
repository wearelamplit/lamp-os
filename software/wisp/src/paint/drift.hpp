// Pure paint-scheduling math: drift slot cadence/rotation/fade roll, and the
// keep-alive deadline/fade formulas. No Arduino deps so it unit-tests in the
// native env and both the engine and tests share one source.
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

// True once a lamp's cached paint target has gone unrefreshed for
// keepaliveMs, regardless of which send (drift, newcomer, keep-alive) last
// stamped it, or forceDue is set (a send FAIL re-opened the deadline before
// keepaliveMs has actually elapsed, e.g. within the boot window). Millis-
// wraparound-safe (unsigned subtraction).
inline bool keepaliveDue(uint32_t nowMs, uint32_t lastPaintMs, uint32_t keepaliveMs,
                          bool forceDue = false) {
  return forceDue || (nowMs - lastPaintMs) >= keepaliveMs;
}

// True once at least minGapMs has elapsed since the last unicast send, any
// walk. Widens the real inter-send spacing past each walk's own per-peer
// pace so a no-ACK frame has room to ACK before the next goes out.
inline bool unicastGateOpen(uint32_t nowMs, uint32_t lastUnicastMs, uint32_t minGapMs) {
  return (nowMs - lastUnicastMs) >= minGapMs;
}

// Keep-alive re-paint fade length: continues an in-flight fade to its
// original fadeEndMs (no acceleration, no jerk), or floors at minFadeMs once
// the fade has already settled, i.e. a steady hold or a lamp recovering
// after a drop.
inline uint32_t reaffirmFadeMs(uint32_t nowMs, uint32_t fadeEndMs, uint32_t minFadeMs) {
  const uint32_t remaining = fadeEndMs > nowMs ? fadeEndMs - nowMs : 0;
  return remaining > minFadeMs ? remaining : minFadeMs;
}

}  // namespace wisp
