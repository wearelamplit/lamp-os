#pragma once

#include <esp_timer.h>

#include <cstdint>

namespace wisp {

// Milliseconds since boot on a 64-bit clock that never wraps; millis()
// restarts from 0 every 49.7 days.
inline uint64_t uptimeMs() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

}  // namespace wisp
