#pragma once

#include <cstdint>
#include <cstring>

#include <lampos/clock.hpp>

namespace lamp {

// Gates "near enough to greet and list" for a direct ESP-NOW HELLO. A BT
// sighting is ungated: the beacon is short-range, so any sighting counts as near.
// ponytail: fixed threshold, no adaptive proximity; calibrate against
// production TX power (ESP-NOW uncapped) if it drifts.
inline constexpr int8_t kNearRssiEspNow = -85;

inline bool isNearRssi(int8_t rssi, int8_t threshold) {
  return rssi != -127 && rssi >= threshold;
}

// Must return the same boolean LampRoster::snapshotNear filters on; a divergence
// would split what BLE reports as near from what roster filtering treats
// as near.
inline bool isNearNow(uint32_t lastSeenNearMs, uint32_t nowMs, uint32_t maxAgeMs) {
  return lastSeenNearMs != 0 && (nowMs - lastSeenNearMs) <= maxAgeMs;
}

// The more recent of a peer's two sighting stamps, 0 meaning never, judged by
// age at nowMs so the choice holds across the millis() wrap.
inline uint32_t mostRecentSeenMs(uint32_t lastSeenNearMs, uint32_t lastSeenMeshMs,
                                 uint32_t nowMs) {
  if (lastSeenNearMs == 0) return lastSeenMeshMs;
  if (lastSeenMeshMs == 0) return lastSeenNearMs;
  return lampos::msSince(nowMs, lastSeenNearMs) <= lampos::msSince(nowMs, lastSeenMeshMs)
             ? lastSeenNearMs
             : lastSeenMeshMs;
}

// Age at nowMs of a peer's most recent sighting on either transport;
// UINT32_MAX for a peer never seen.
inline uint32_t sinceLastSeen(uint32_t lastSeenNearMs, uint32_t lastSeenMeshMs,
                              uint32_t nowMs) {
  const uint32_t seen = mostRecentSeenMs(lastSeenNearMs, lastSeenMeshMs, nowMs);
  return seen == 0 ? UINT32_MAX : lampos::msSince(nowMs, seen);
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
  const uint32_t aAge = sinceLastSeen(a.lastSeenNearMs, a.lastSeenMeshMs, nowMs);
  const uint32_t bAge = sinceLastSeen(b.lastSeenNearMs, b.lastSeenMeshMs, nowMs);
  if (aAge != bAge) return aAge < bAge;
  return a.lastRssi > b.lastRssi;
}

}  // namespace lamp
