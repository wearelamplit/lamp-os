#pragma once

#include <cstdint>

// Comparisons on 32-bit millis() stamps that hold across its 49.7-day wrap.
namespace lampos {

// How far ahead of a reader's nowMs a stamp another task wrote after it may
// sit, such as across a loop stall.
inline constexpr uint32_t kStampLeadMs = 60000;

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

// Age of `stampMs` at nowMs. A stamp up to kStampLeadMs ahead of nowMs reads
// as age 0, so ages hold up to 49.7 days less kStampLeadMs.
constexpr uint32_t msSince(uint32_t nowMs, uint32_t stampMs) {
  const uint32_t age = nowMs - stampMs;
  return age > UINT32_MAX - kStampLeadMs ? 0 : age;
}

// `stampMs` once it has aged past windowMs, held at windowMs + 1 old so it
// keeps reading as outside the window instead of coming round to read as
// recent one wrap later; any other stamp comes back unchanged. 0 means never
// set and comes back as is, and a set stamp never comes back as 0. The owner
// calls it at least once per wrap.
constexpr uint32_t retireStamp(uint32_t nowMs, uint32_t stampMs, uint32_t windowMs) {
  if (stampMs == 0 || msSince(nowMs, stampMs) <= windowMs) return stampMs;
  const uint32_t held = nowMs - windowMs - 1;
  return held != 0 ? held : UINT32_MAX;
}

}  // namespace lampos
