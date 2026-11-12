#include "util/dropped_write_log.hpp"

#ifdef LAMP_DEBUG
#include <Arduino.h>

#include "util/log_rate_limiter.hpp"
#include "util/uptime.hpp"

namespace lamp {

namespace {

// A colour drag streams writes; a dropped stream prints a few lines a second
// and counts the rest.
constexpr uint32_t kWindowMs = 1000;
constexpr uint32_t kLinesPerWindow = 4;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
LogRateLimiter s_limiter(kWindowMs, kLinesPerWindow);

}  // namespace

void logDroppedWrite(const char* characteristic, const char* reason) {
  // Read the clock inside the lock, since a timestamp older than the window's
  // start reopens it.
  portENTER_CRITICAL(&s_mux);
  const LogRateLimiter::Admission a = s_limiter.admit(uptimeMs());
  portEXIT_CRITICAL(&s_mux);
  if (!a.print) return;
  if (a.suppressed == 0) {
    Serial.printf("[drop] %s: %s\n", characteristic, reason);
  } else {
    Serial.printf("[drop] %s: %s (+%u suppressed)\n", characteristic, reason,
                  (unsigned)a.suppressed);
  }
}

}  // namespace lamp
#endif
