#pragma once

#include <esp_timer.h>

#include <cstdint>

namespace lamp {

// Milliseconds since boot on a 64-bit clock that never wraps. Use it for
// "first N ms after boot" windows; millis() restarts from 0 every 49.7 days.
inline uint64_t uptimeMs() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

}  // namespace lamp
