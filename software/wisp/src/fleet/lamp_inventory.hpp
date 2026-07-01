#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wisp {

// Mirror lamp-os's prune window so a peer that drops off the mesh disappears
// from wisp's roster at the same wall-clock moment it disappears from peer
// lamps' nearby lists. 2 minutes.
#ifndef LAMP_PRUNE_TIME_MS
#define LAMP_PRUNE_TIME_MS 120000
#endif

// One lamp wisp has heard from. Keyed by MAC because name can change live via
// the app (and wisp doesn't want to fold two MACs onto one name when a user
// renames mid-session). baseColor + shadeColor track the lamp's currently
// advertised personality; firmwareVersion gates the OTA picker.
struct InventoryEntry {
  uint8_t mac[6] = {0};
  std::string name;
  uint8_t baseColor[4] = {0};   // RGBW
  uint8_t shadeColor[4] = {0};  // RGBW
  uint32_t firmwareVersion = 0;
  uint32_t lastSeenMs = 0;
  // RSSI of the most recent MSG_HELLO from this lamp, as reported by
  // ESP-NOW. Used by the WispRoster claim-decision logic to figure out
  // which wisp is closest to each lamp. Defaults to INT8_MIN to mean
  // "never measured" — entries that came in via a code path that didn't
  // surface RSSI keep this sentinel and won't be chosen as the closer
  // wisp until a real measurement lands.
  int8_t rssi = INT8_MIN;
};

/**
 * @brief Wisp's roster of lamps on the mesh.
 *
 * Fed from MeshLink's recv handler (on a non-Arduino task in practice — the
 * ESP-NOW driver runs callbacks from a high-priority WiFi task). Read from
 * the loop task. Single mutex guards everything; critical sections are kept
 * short (no logging inside the lock).
 *
 * Consumers:
 *   - serial dump every 10s for bench debug;
 *   - PaintDistributor walks the snapshot to fan out paint;
 *   - StatusBeacon includes the count in HELLO + surfaces the snapshot
 *     to the app via the BLE proxy.
 */
class LampInventory {
 public:
  LampInventory();

  // Called from MeshLink recv handler (WiFi task). Bounded mutex take so a
  // contended loop reader can't stall recv; on contention we drop the update
  // — HELLOs repeat every 2 s, so the lamp is caught on the next beacon.
  // `rssi` is the signed ESP-NOW RX RSSI for the frame that delivered this
  // hello, or INT8_MIN to mean "no measurement available" (test rigs etc.).
  void recordHello(const uint8_t mac[6], const std::string& name,
                   const uint8_t baseRGBW[4], const uint8_t shadeRGBW[4],
                   uint32_t firmwareVersion, uint32_t nowMs,
                   int8_t rssi = INT8_MIN);

  // Drop entries older than maxAgeMs. Call periodically from loop().
  void prune(uint32_t nowMs, uint32_t maxAgeMs);

  // Copy of the current roster. Caller can do whatever they want with it
  // without holding the lock. Cost is O(N) memcpy; N <= MAX_LAMPS.
  std::vector<InventoryEntry> snapshot();

  // Cheap count for diagnostics; doesn't allocate.
  size_t size();

  static constexpr size_t MAX_LAMPS = 32;

 private:
  // Find by MAC. Caller holds the mutex. Returns entries_.size() if missing.
  size_t findByMacLocked(const uint8_t mac[6]) const;
  void evictOldestIfFullLocked(uint32_t nowMs);

  std::vector<InventoryEntry> entries_;
  // Opaque to keep the FreeRTOS dependency out of the header (so host
  // tests can include this file without pulling in FreeRTOS).
  void* mutex_ = nullptr;
};

}  // namespace wisp
