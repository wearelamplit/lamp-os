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

// Oldest age retireStamp lets a stamp reach, just under half a wrap: 24.8
// days.
inline constexpr uint32_t kRetiredAgeMs = INT32_MAX;

// `stampMs` held no more than kRetiredAgeMs old, so it reads as outside any
// window shorter than that and never comes round to read as recent one wrap
// later. A younger stamp, or one up to kStampLeadMs ahead, comes back
// unchanged, and a held stamp is never 0. The owner calls it at least once
// per kRetiredAgeMs, and skips a stamp whose 0 it reads as never set.
constexpr uint32_t retireStamp(uint32_t nowMs, uint32_t stampMs) {
  if (msSince(nowMs, stampMs) <= kRetiredAgeMs) return stampMs;
  const uint32_t held = nowMs - kRetiredAgeMs;
  return held != 0 ? held : UINT32_MAX;
}

}  // namespace lampos
