#include "status/config_cadence.hpp"

#include <lampos/clock.hpp>

namespace wisp {

bool ConfigCadence::bursting(uint32_t nowMs) const {
  return burst_ && static_cast<int32_t>(nowMs - burstUntilMs_) < 0;
}

void ConfigCadence::onStatusEmitted(uint32_t nowMs, uint32_t configHash) {
  haveStatus_ = true;
  lastStatusMs_ = nowMs;
  if (burst_ && !bursting(nowMs)) burst_ = false;
  if (!haveHash_ || configHash != hash_) {
    // The boot fingerprint seeds the lamps' caches without a burst.
    if (haveHash_) {
      burst_ = true;
      burstUntilMs_ = nowMs + kBurstMs;
    }
    haveHash_ = true;
    hash_ = configHash;
    pending_ = true;
  } else if (bursting(nowMs)) {
    pending_ = true;
  }
}

void ConfigCadence::poll() { pending_ = true; }

bool ConfigCadence::configDue(uint32_t nowMs) const {
  if (!pending_) return false;
  if (haveStatus_ && nowMs - lastStatusMs_ < kGapMs) return false;
  const uint32_t interval = bursting(nowMs) ? kBurstIntervalMs : kMinIntervalMs;
  return !haveConfig_ || nowMs - lastConfigMs_ >= interval;
}

void ConfigCadence::onConfigEmitted(uint32_t nowMs) {
  pending_ = false;
  haveConfig_ = true;
  lastConfigMs_ = nowMs;
}

void ConfigCadence::retire(uint32_t nowMs) {
  lastConfigMs_ = lampos::retireStamp(nowMs, lastConfigMs_);
}

bool ConfigCadence::statusClear(uint32_t nowMs) const {
  return !haveConfig_ || nowMs - lastConfigMs_ >= kGapMs;
}

}  // namespace wisp
