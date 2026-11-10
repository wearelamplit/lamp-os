#pragma once

#include <cstdint>

// Comparisons on 32-bit millis() stamps that hold across its 49.7-day wrap.
namespace lampos {

// A stamp another task wrote just after the reader took nowMs reads as up to
// this far in the future.
inline constexpr uint32_t kStampSkewMs = 1000;

// True once nowMs has reached deadlineMs. Holds while the deadline lies within
// 2^31 ms (24.8 days) of nowMs on either side.
constexpr bool timeReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

// True when stamp `a` falls after stamp `b`; the two must lie within 24.8 days
// of each other.
constexpr bool isLater(uint32_t a, uint32_t b) {
  return static_cast<int32_t>(a - b) > 0;
}

// Age of `stampMs` at nowMs, for stamps up to 49.7 days old. A stamp up to
// kStampSkewMs ahead of nowMs reads as age 0.
constexpr uint32_t msSince(uint32_t nowMs, uint32_t stampMs) {
  const uint32_t age = nowMs - stampMs;
  return age >= UINT32_MAX - kStampSkewMs ? 0 : age;
}

}  // namespace lampos
