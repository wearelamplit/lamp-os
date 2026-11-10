#pragma once

#include <cstdint>

namespace lamp {

// Caps a repeating log line at `linesPerWindow` per `windowMs`. Not
// thread-safe: a caller logging from more than one task holds a lock across
// admit(). Pass lamp::uptimeMs(): a window stamp on 32-bit millis() read
// weeks later can still look open.
class LogRateLimiter {
 public:
  struct Admission {
    bool print;
    // Lines refused since the last admitted one. Set only when print is true.
    uint32_t suppressed;
  };

  LogRateLimiter(uint32_t windowMs, uint32_t linesPerWindow);

  Admission admit(uint64_t nowMs);

 private:
  uint32_t windowMs_;
  uint32_t linesPerWindow_;
  uint64_t windowStartMs_ = 0;
  uint32_t used_ = 0;
  uint32_t suppressed_ = 0;
};

}  // namespace lamp
