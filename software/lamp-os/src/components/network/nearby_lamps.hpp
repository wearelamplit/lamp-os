#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <string>
#include <vector>

#include "lamp_protocol.hpp"
#include "util/color.hpp"
#include "util/proximity.hpp"
#include "wisp_cache.hpp"

// Default prune age (milliseconds) shared between callers. NimBLE scan
// timer uses this when it runs onScanEnd; if a lamp hasn't been heard on
// EITHER transport in this window, it's dropped.
#ifndef LAMP_PRUNE_TIME_MS
#define LAMP_PRUNE_TIME_MS 120000
#endif

namespace lamp {

/**
 * @brief One nearby lamp observed via BLE manufacturer-data scan,
 *        ESP-NOW HELLO, or both. Primary key is `name` (user-set, capped
 *        12 chars); preserves BLE-only lamp compatibility.
 *        `bdAddr` is a stable secondary identifier: renaming a lamp keeps
 *        the same BD_ADDR, so dispositions don't orphan.
 *
 *        Per-transport `lastSeenVia*Ms` track when we last heard from
 *        this lamp on each channel so consumers can filter:
 *
 *        - SocialBehavior cares about lastSeenViaBleMs (short-range
 *          intimacy); it doesn't greet 200 m peers.
 *        - The grid view + remote-config flow gate on lastSeenViaEspNowMs
 *          since CHAR_REMOTE_OP forwarding only works for ESP-NOW peers.
 *        - The app's "Nearby Lamps" list shows everything.
 *
 *        `mac` is only populated once we've heard at least one HELLO —
 *        BLE adv doesn't carry a stable lamp-MAC anyway (the BT MAC isn't
 *        the WiFi STA MAC the protocol uses for addressing).
 */
struct NearbyLamp {
  std::string name;
  Color baseColor = Color();
  Color shadeColor = Color();
  uint8_t mac[6] = {0};
  // Canonical uppercase colon-hex BLE Device Address (e.g. "AA:BB:CC:DD:EE:FF").
  // Stable for the entry's lifetime (BD_ADDR doesn't change on rename).
  // Used as the per-peer disposition key. Empty until the first BLE advert.
  std::string bdAddr;
  bool hasMac = false;
  uint32_t lastSeenViaBleMs = 0;
  uint32_t lastSeenViaEspNowMs = 0;

  // Stamped on the first sighting; stable for the entry's lifetime.
  // Arrival detection: if (peer.firstSeenMs >= lastTickMs_) { /* just arrived */ }
  uint32_t firstSeenMs = 0;

  bool acknowledged = false;  // SocialBehavior's per-name greeting state
  // Packed semver (major<<16|minor<<8|patch) from MSG_HELLO.
  // Zero until the first HELLO (BLE adv doesn't carry it).
  uint32_t firmwareVersion = 0;
  // OTA state from HELLO_TLV_OTA_STATE. 0/1/2 = idle/sending/receiving.
  // Only set via the ESP-NOW path (peers' BLE adv doesn't carry it).
  // Defaults to idle, matches the protocol's "no TLV present = idle"
  // semantics.
  uint8_t otaState = 0;
  // Protocol version byte from the HELLO frame header. Used by the OTA
  // distributor to build OFFER/CHUNK/DONE at the peer's version.
  // Zero until a HELLO arrives (no OTA until the peer's version is known).
  uint8_t protocolVersion = 0;
  // Peer's `{type}-{channel}` slot from HELLO_TLV_FW_CHANNEL (e.g. "standard-beta").
  // Empty on older peers that predate the TLV; OTA distributor skips cross-type OFFERs.
  // 16 bytes + NUL matching FW_CHANNEL_LEN.
  char fwChannel[17] = {0};
  // Peer's FS-image digest prefix from HELLO_TLV_FS_STATE.
  // hasFsDigest=false on older peers. The FS OTA distributor compares
  // this to the local digest to decide whether to offer the UI image.
  bool    hasFsDigest = false;
  uint8_t fsDigest[8] = {0};
  // BLE-scan RSSI (dBm). Written only by addOrUpdateFromBle (single-transport
  // invariant for PersonalityEngine hysteresis). -127 = unknown, sorts to back.
  // getReachableViaBle() sorts descending; getReachableViaEspNow() does not.
  int8_t lastRssi = -127;
};

/**
 * @brief Single source of truth for "lamps I can hear right now."
 *        Mutated from NimBLE scan callback (Core 0) and ESP-NOW HELLO
 *        recv (WiFi task); read from the loop task. SemaphoreHandle_t
 *        mutex serialises.
 */
class NearbyLamps {
 public:
  static constexpr size_t MAX_NEARBY = 32;

  NearbyLamps();

  void addOrUpdateFromBle(const std::string& name,
                          const std::string& bdAddr,
                          const Color& base, const Color& shade,
                          int8_t rssi = -127);
  void addOrUpdateFromEspNow(const std::string& name, const uint8_t mac[6],
                             const Color& base, const Color& shade,
                             uint32_t firmwareVersion = 0,
                             uint8_t otaState = 0,
                             uint8_t protocolVersion = 0,
                             const char* fwChannel = nullptr,
                             const uint8_t* fsDigest = nullptr,
                             bool hasFsDigest = false);

  // Drop entries whose most-recent sighting (max of the two transports)
  // is older than `maxAgeMs`.
  void prune(uint32_t maxAgeMs);

  // SocialBehavior: only entries whose lastSeenViaBleMs is within maxAgeMs.
  std::vector<NearbyLamp> getReachableViaBle(uint32_t maxAgeMs);

  // Grid view / remote-config: only entries whose lastSeenViaEspNowMs
  // is within maxAgeMs.
  std::vector<NearbyLamp> getReachableViaEspNow(uint32_t maxAgeMs);

  // Full snapshot, used by CHAR_NEARBY_LAMPS for the app's unified list.
  std::vector<NearbyLamp> getAll();

  // Look up a peer by BD_ADDR (uppercase colon-hex). Returns true and fills
  // [out] on hit; false on miss with [out] untouched.
  bool findByBdAddr(const std::string& bdAddr, NearbyLamp& out);

  // Returns true and fills [out] on hit; false on miss. Only matches entries
  // with hasMac=true; BLE-only entries (no HELLO received) are excluded.
  // Used by the OTA progress overlay to fetch the active peer's color.
  bool findByMac(const uint8_t mac[6], NearbyLamp& out);

  // Mark a lamp as acknowledged. SocialBehavior calls this once per peer
  // so a re-trigger doesn't re-greet the same lamp until it prunes.
  void acknowledge(const std::string& name);

  // Cache wisp presence from MSG_WISP_HELLO; most recent hello wins.
  void cacheWispHello(const uint8_t mac[6],
                      uint32_t wispVersion,
                      uint8_t flags,
                      const char* paletteIdPrefix,  // 8 bytes; not NUL-terminated
                      const char* carriedFwChannel, // 8 bytes; not NUL-terminated
                      uint32_t carriedFwVersion);

  // Snapshot of the wisp cache. Returns a copy so the brightness-floor
  // check can compare against `mac` and `lastHelloMs` without holding
  // any lock past the read.
  WispCache getWispCache();

  // Cache the latest wispStatus JSON broadcast for a given wisp MAC.
  // Loop-task-only writer (Core 1). A status broadcast from a
  // previously-unseen wisp asserts presence even before a hello arrives.
  void cacheWispStatus(const uint8_t mac[6],
                       const char* json, size_t jsonLen);

  // Cache the latest MSG_WISP_PALETTE for a given wisp MAC.
  // `rgb` is `count * 3` packed R,G,B bytes; `count` is clamped to
  // kMaxWispPaletteColors. Loop-task-only writer (Core 1).
  void cacheWispPalette(const uint8_t mac[6],
                        const uint8_t* rgb, uint8_t count);

  // Cache the latest MSG_WISP_CLAIM roster for a given wisp MAC.
  // `lampMacs` is [count][6] MAC bytes. `nowMs` feeds the staleness
  // check in buildWispClaimsBlob. Loop-task-only writer (Core 1).
  void cacheWispClaim(const uint8_t mac[6],
                      const uint8_t lampMacs[][6], uint8_t count,
                      uint32_t nowMs);

  // Build the binary blob served on CHAR_WISP_CLAIMS: [count:1][lampMac:6]*count.
  // Returns count=0 when the cached claim is stale (nowMs - lastClaimMs >
  // kWispClaimStaleMs). Returns the number of bytes written to `out`.
  // Max output = 1 + kMaxWispClaimEntries * 6 = 193 bytes.
  size_t buildWispClaimsBlob(uint8_t* out, size_t outCap, uint32_t nowMs);

  // Assert wisp presence from a paint frame. Unicast OVERRIDE_COLORS
  // can arrive before gossip WISP_HELLO; this pins the MAC so wispStatus
  // is non-empty on the first BLE read. hello-derived fields are not set.
  // 2 ms bounded take: CHAR_WISP_STATUS reads on Core 0 share this mutex.
  void cacheWispMacFromPaint(const uint8_t mac[6]);

  // Build and return the JSON to serve on CHAR_WISP_STATUS reads.
  // Merges the cached wispStatus payload with the last MSG_WISP_HELLO
  // data. Returns "{}" if nothing has been cached for either path.
  std::string getWispStatusReadJson(bool includePalette = false);

  // Current wisp-control state per surface: whether each surface is
  // actively wisp-painted and the most recent paint color.
  // Set via setLampWispStateProvider() to keep ColorOverride coupling in lamp.cpp.
  struct LampWispState {
    bool        controllingBase = false;
    bool        controllingShade = false;
    std::string baseWispColor;   // 8-char hex like "AB12FF40", or empty
    std::string shadeWispColor;  // same
  };
  using LampWispStateProvider = LampWispState (*)();
  void setLampWispStateProvider(LampWispStateProvider provider) {
    lampWispStateProvider_ = provider;
  }

 private:
  LampWispStateProvider lampWispStateProvider_ = nullptr;
  std::vector<NearbyLamp> store_;
  SemaphoreHandle_t mutex_ = nullptr;
  WispCache wispCache_;

  // Caller must hold the mutex. Returns index of entry or store_.size()
  // if not found.
  size_t findIndexLocked(const std::string& name) const;
  // Caller must hold the mutex. Evicts the entry with the oldest combined
  // last-seen if the store is at capacity.
  void evictOldestIfFullLocked();
};

extern NearbyLamps nearbyLamps;  // single global instance, defined in .cpp

}  // namespace lamp
