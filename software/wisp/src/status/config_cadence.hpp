#pragma once

#include <cstdint>

#include "status/status_cadence.hpp"

namespace wisp {

// Decides when wispConfig goes out. A config is sent after a status emit that
// found it changed, again after every status emit for the rest of that
// change's burst, and once per pollConfig, coalesced. A config and a status
// never go out within kGapMs of each other: a lamp holds one inbound control
// op per loop pass, so of two back-to-back frames the first is lost.
// Loop task only.
class ConfigCadence {
 public:
  static constexpr uint32_t kGapMs = 250;
  static constexpr uint32_t kMinIntervalMs = 5000;
  static constexpr uint32_t kBurstMs = StatusCadence::kBurstMs;
  static constexpr uint32_t kBurstIntervalMs = StatusCadence::kBurstIntervalMs;

  // After each wispStatus emit, with a fingerprint of the current config.
  void onStatusEmitted(uint32_t nowMs, uint32_t configHash);
  void poll();
  bool configDue(uint32_t nowMs) const;
  void onConfigEmitted(uint32_t nowMs);
  // Retires the last-config stamp, which a quiet wisp can leave for weeks, so
  // it cannot hold a status or a config one millis() wrap later. Call at least
  // once per lampos::kRetiredAgeMs.
  void retire(uint32_t nowMs);
  // False while a status would land too close behind the last config.
  bool statusClear(uint32_t nowMs) const;

 private:
  bool bursting(uint32_t nowMs) const;

  bool pending_ = false;
  bool haveHash_ = false;
  uint32_t hash_ = 0;
  bool haveStatus_ = false;
  uint32_t lastStatusMs_ = 0;
  bool haveConfig_ = false;
  uint32_t lastConfigMs_ = 0;
  bool burst_ = false;
  uint32_t burstUntilMs_ = 0;
};

}  // namespace wisp
