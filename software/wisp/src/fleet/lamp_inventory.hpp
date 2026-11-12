#pragma once

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "wire/lamp_protocol.hpp"

namespace wisp {

// Matches the lamp prune window so lamps disappear from both at the same wall-clock moment.
#ifndef LAMP_PRUNE_TIME_MS
#define LAMP_PRUNE_TIME_MS 240000
#endif

// A direct RSSI older than this is forgotten. Three lamp HELLO intervals: a
// lamp that loses two beacons in a row lapses only when a prune lands in the
// moments before its third arrives.
constexpr uint32_t kDirectRssiMaxAgeMs =
    3 * lamp_protocol::kLampHelloIntervalMs;

// Minimal (mac, rssi) view of an inventory entry, sized for stack arrays.
// rssi INT8_MIN means no current direct measurement.
struct LampObservation {
  uint8_t mac[6] = {0};
  int8_t rssi = INT8_MIN;
};

// One lamp wisp has heard from. Keyed by MAC because name can change live via
// the app (and wisp doesn't want to fold two MACs onto one name when a user
// renames mid-session). baseColor + shadeColor track the lamp's currently
// advertised personality; firmwareVersion gates the OTA picker.
// Trivially copyable: snapshots are frequent and per-entry heap strings
// fragment a small heap.
struct InventoryEntry {
  uint8_t mac[6] = {0};
  char name[lamp_protocol::HELLO_MAX_NAME + 1] = {0};
  uint8_t baseColor[4] = {0};   // RGBW
  uint8_t shadeColor[4] = {0};  // RGBW
  uint32_t firmwareVersion = 0;
  uint32_t lastSeenMs = 0;
  // RSSI of the most recent MSG_HELLO this wisp heard directly from the lamp,
  // stamped at rssiMs. WispRoster's claim arbitration compares it with peer
  // wisps'. INT8_MIN means no current direct measurement: never heard
  // directly, or aged out past kDirectRssiMaxAgeMs.
  int8_t rssi = INT8_MIN;
  uint32_t rssiMs = 0;
};

static_assert(std::is_trivially_copyable<InventoryEntry>::value,
              "inventory snapshots must not touch the heap per entry");

/**
 * Wisp's roster of lamps on the mesh.
 *
 * Fed from MeshLink's recv handler (on a non-Arduino task in practice; the
 * ESP-NOW driver runs callbacks from a high-priority WiFi task). Read from
 * the loop task. Single mutex guards everything; critical sections are kept
 * short (no logging inside the lock).
 *
 * Consumers:
 *   - serial dump every 10s for bench debug (snapshot);
 *   - PaintDistributor + WispRoster claim recompute (copyObservations).
 */
class LampInventory {
 public:
  LampInventory();

  // Called from MeshLink recv handler (WiFi task). Bounded mutex take so a
  // contended loop reader can't stall recv; on contention the update is dropped
  // and the lamp is caught on its next beacon.
  // `name` is a null-terminated string, truncated at HELLO_MAX_NAME.
  // `rssi` is the signed ESP-NOW RX RSSI for the frame that delivered this
  // hello, or INT8_MIN to mean "no measurement available" (test rigs etc.).
  void recordHello(const uint8_t mac[6], const char* name,
                   const uint8_t baseRGBW[4], const uint8_t shadeRGBW[4],
                   uint32_t firmwareVersion, uint32_t nowMs,
                   int8_t rssi = INT8_MIN);

  // Drop entries older than maxAgeMs, and forget a direct RSSI older than
  // kDirectRssiMaxAgeMs so a lamp heard only through relays stops counting
  // toward this wisp's claim. Call periodically from loop().
  void prune(uint32_t nowMs, uint32_t maxAgeMs);

  // Copy of the current roster. Caller can do whatever they want with it
  // without holding the lock. Cost is O(N) memcpy; N <= MAX_LAMPS.
  std::vector<InventoryEntry> snapshot();

  // Fill `out` with up to `max` (mac, rssi) observations. No allocation;
  // returns the count written.
  size_t copyObservations(LampObservation* out, size_t max);

  // Cheap count for diagnostics; doesn't allocate.
  size_t size();

  static constexpr size_t MAX_LAMPS = 100;

 private:
  // Find by MAC. Caller holds the mutex. Returns count_ if missing.
  size_t findByMacLocked(const uint8_t mac[6]) const;
  void evictOldestIfFullLocked(uint32_t nowMs);

  std::array<InventoryEntry, MAX_LAMPS> entries_{};
  size_t count_ = 0;
  // Opaque to keep the FreeRTOS dependency out of the header (so host
  // tests can include this file without pulling in FreeRTOS).
  void* mutex_ = nullptr;
};

}  // namespace wisp
