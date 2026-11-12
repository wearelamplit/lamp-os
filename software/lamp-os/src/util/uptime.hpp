#pragma once

#include <esp_timer.h>

#include <cstdint>

namespace lamp {

// Milliseconds since boot on a 64-bit clock that never wraps. Use it for
// "first N ms after boot" windows, and for stamps read only on an event or
// from code home mode can skip for weeks; millis() restarts from 0 every
// 49.7 days.
inline uint64_t uptimeMs() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

// Milliseconds since `stampMs`, an uptimeMs() reading, saturating at
// UINT32_MAX.
inline uint32_t uptimeSince(uint64_t stampMs) {
  const uint64_t age = uptimeMs() - stampMs;
  return age > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(age);
}

}  // namespace lamp
