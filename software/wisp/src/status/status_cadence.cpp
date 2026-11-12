#include "status/status_cadence.hpp"

namespace wisp {

void StatusCadence::request() { due_.store(true, std::memory_order_relaxed); }

void StatusCadence::change() {
  burstRequested_.store(true, std::memory_order_relaxed);
  request();
}

bool StatusCadence::takeEmit(uint32_t nowMs, bool statusClear) {
  if (burstRequested_.exchange(false, std::memory_order_relaxed)) {
    bursting_ = true;
    burstUntilMs_ = nowMs + kBurstMs;
  }
  if (bursting_ && static_cast<int32_t>(nowMs - burstUntilMs_) >= 0) {
    bursting_ = false;
  }
  if (!due_.load(std::memory_order_relaxed) && !bursting_) return false;
  const uint32_t minInterval = bursting_ ? kBurstIntervalMs : kMinIntervalMs;
  if (haveEmitted_ && nowMs - lastEmitMs_ < minInterval) return false;
  if (!statusClear) return false;
  due_.store(false, std::memory_order_relaxed);
  haveEmitted_ = true;
  lastEmitMs_ = nowMs;
  return true;
}

}  // namespace wisp
