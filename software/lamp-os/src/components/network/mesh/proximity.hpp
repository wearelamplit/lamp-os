#pragma once

#include <cstdint>
#include <cstring>

namespace lamp {

// Gates "physically close enough to greet" for a direct ESP-NOW HELLO.
// ESP-NOW transmits hotter than BLE adv, so this sits high (less negative)
// to land on the same ~5-10 m bubble as a BLE sighting.
// A BT sighting is ungated: the beacon is short-range, so any sighting
// counts as near.
// ponytail: fixed threshold, no adaptive proximity; calibrate against
// production TX power (ESP-NOW uncapped) if it drifts.
inline constexpr int8_t kNearRssiEspNow = -64;

inline bool isNearRssi(int8_t rssi, int8_t threshold) {
  return rssi != -127 && rssi >= threshold;
}

// Must return the same boolean LampRoster::getNear computes; a divergence
// would split what BLE reports as near from what roster filtering treats
// as near.
inline bool isNearNow(uint32_t lastSeenNearMs, uint32_t nowMs, uint32_t maxAgeMs) {
  return lastSeenNearMs != 0 && (nowMs - lastSeenNearMs) <= maxAgeMs;
}

inline bool isDirectHello(const uint8_t frameSrc[6],
                          const uint8_t helloOriginator[6]) {
  return std::memcmp(frameSrc, helloOriginator, 6) == 0;
}

// Relevance order for the app-facing nearby list: near peers before mesh-only,
// then most-recently-seen, then strongest RSSI. Strict-weak-ordering (a
// lexicographic compose of total preorders), so ble_control's partial_sort
// cap keeps the front-N and drops the farthest/stalest. Templated on the entry
// so this header stays free of the RosterEntry (and its Arduino) dependency.
template <typename Entry>
bool nearbyMoreRelevant(const Entry& a, const Entry& b, uint32_t nowMs,
                        uint32_t maxAgeMs) {
  const bool an = isNearNow(a.lastSeenNearMs, nowMs, maxAgeMs);
  const bool bn = isNearNow(b.lastSeenNearMs, nowMs, maxAgeMs);
  if (an != bn) return an;
  const uint32_t as =
      a.lastSeenNearMs > a.lastSeenMeshMs ? a.lastSeenNearMs : a.lastSeenMeshMs;
  const uint32_t bs =
      b.lastSeenNearMs > b.lastSeenMeshMs ? b.lastSeenNearMs : b.lastSeenMeshMs;
  if (as != bs) return as > bs;
  return a.lastRssi > b.lastRssi;
}

}  // namespace lamp
