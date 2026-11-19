#pragma once

#include <cstdint>

#include <lampos/clock.hpp>
#include <lampos/protocol/presence.hpp>

#include "components/network/protocol/fw_ota.hpp"

namespace lamp {

// A peer's ESP-NOW RSSI as measured off its own direct HELLOs, the link an OTA
// offer to it would use. Fed only direct readings: a relayed copy measures the
// relayer's link. Trivially copyable, so it lives in the roster entry.
class DirectRssi {
 public:
  // Age past which a reading is stale. At the steady HELLO interval one lost
  // HELLO keeps the reading and two in a row expire it.
  static constexpr uint32_t kMaxAgeMs = 5 * lamp_protocol::kLampHelloIntervalMs / 2;
  // How long a link must hold the OTA floor before an offer, and the fewest
  // readings that prove it. A steady 30 s HELLO gives two readings in 45 s.
  static constexpr uint32_t kOtaHoldMs = 45000;
  static constexpr uint8_t kOtaMinReadings = 2;

  // Records a direct HELLO's RSSI heard at nowMs; -127 (unavailable) is
  // ignored. A reading below kOtaMinRssiDbm, or one after the last went stale,
  // restarts the hold.
  void record(int8_t rssi, uint32_t nowMs) {
    if (rssi == -127) return;
    const bool continues = readings_ > 0 && isFresh(nowMs);
    const uint32_t gap = continues ? lampos::msSince(nowMs, lastMs_) : 0;
    rssi_ = rssi;
    lastMs_ = nowMs;
    if (rssi < lamp_protocol::kOtaMinRssiDbm) {
      readings_ = 0;
      heldMs_ = 0;
      return;
    }
    if (!continues) {
      readings_ = 0;
      heldMs_ = 0;
    }
    if (readings_ < UINT8_MAX) readings_++;
    const uint32_t held = heldMs_ + gap;
    heldMs_ = held < UINT16_MAX ? static_cast<uint16_t>(held) : UINT16_MAX;
  }

  // The latest reading, or -127 when there is none or it is stale.
  int8_t fresh(uint32_t nowMs) const { return isFresh(nowMs) ? rssi_ : -127; }

  // True when the link has held kOtaMinRssiDbm or better for kOtaHoldMs,
  // across at least kOtaMinReadings readings, and the latest is fresh.
  bool holdsOtaFloor(uint32_t nowMs) const {
    if (readings_ < kOtaMinReadings || !isFresh(nowMs)) return false;
    return heldMs_ + lampos::msSince(nowMs, lastMs_) >= kOtaHoldMs;
  }

  // Keeps an old reading stale across the millis() wrap; the owner calls it
  // at least once per lampos::kRetiredAgeMs.
  void retire(uint32_t nowMs) {
    if (rssi_ != -127) lastMs_ = lampos::retireStamp(nowMs, lastMs_);
  }

 private:
  bool isFresh(uint32_t nowMs) const {
    return rssi_ != -127 && lampos::msSince(nowMs, lastMs_) <= kMaxAgeMs;
  }

  uint32_t lastMs_ = 0;
  // Span of the current hold up to lastMs_. Saturates well past kOtaHoldMs,
  // which is all it is compared against, so the entry stays 8 bytes.
  uint16_t heldMs_ = 0;
  uint8_t readings_ = 0;
  int8_t rssi_ = -127;
};

static_assert(DirectRssi::kOtaHoldMs < UINT16_MAX, "heldMs_ must reach the hold");
static_assert(sizeof(DirectRssi) == 8, "one DirectRssi per roster entry");

}  // namespace lamp
