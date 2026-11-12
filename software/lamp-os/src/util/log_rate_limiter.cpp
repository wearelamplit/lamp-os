#include "util/log_rate_limiter.hpp"

namespace lamp {

LogRateLimiter::LogRateLimiter(uint32_t windowMs, uint32_t linesPerWindow)
    : windowMs_(windowMs), linesPerWindow_(linesPerWindow) {}

LogRateLimiter::Admission LogRateLimiter::admit(uint64_t nowMs) {
  if (nowMs - windowStartMs_ >= windowMs_) {
    windowStartMs_ = nowMs;
    used_ = 0;
  }
  if (used_ >= linesPerWindow_) {
    ++suppressed_;
    return {false, 0};
  }
  ++used_;
  const uint32_t suppressed = suppressed_;
  suppressed_ = 0;
  return {true, suppressed};
}

}  // namespace lamp
