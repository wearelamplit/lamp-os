#pragma once

#include <cstdint>

namespace lamp {

// Three-bucket proximity classifier driven by raw RSSI. Used by
// buildNearbyLampsJson to emit a proximity tier per peer for the app.
//
// Thresholds bench-calibrated 2026-06-17:
//   jacko 5" / other 10' → Near (-72 dBm)
//   oatmilky next room through wall → Around (-84 dBm)
//   vamp far end of living room → Far (-97 dBm)
//
// Aligned with firmware BLE_MINIMUM_RSSI_VALUE = -94 floor — anything
// below -94 dBm is dropped at the scan callback in bluetooth.cpp:103
// and never sees this function. The -127 "no RSSI" sentinel falls
// into Far which is the safe-display default.
enum class Proximity : uint8_t { Near = 0, Around = 1, Far = 2 };

constexpr int8_t kProximityNearMin   = -80;  // >= -80 → Near
constexpr int8_t kProximityAroundMin = -90;  // -89..-81 → Around; < -90 → Far

inline Proximity proximityFor(int8_t rssi) {
  if (rssi >= kProximityNearMin)   return Proximity::Near;
  if (rssi >= kProximityAroundMin) return Proximity::Around;
  return Proximity::Far;
}

inline uint8_t proximityToInt(Proximity p) {
  return static_cast<uint8_t>(p);
}

}  // namespace lamp
