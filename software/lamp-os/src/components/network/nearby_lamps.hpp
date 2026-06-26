#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <string>
#include <vector>

#include "util/color.hpp"
#include "util/proximity.hpp"

// Default prune age (milliseconds) shared between callers. NimBLE scan
// timer uses this when it runs onScanEnd; if a lamp hasn't been heard on
// EITHER transport in this window, it's dropped.
#ifndef LAMP_PRUNE_TIME_MS
#define LAMP_PRUNE_TIME_MS 120000
#endif

namespace lamp {

/**
 * @brief One nearby lamp — observed via BLE manufacturer-data scan,
 *        ESP-NOW HELLO, or both. PRIMARY KEY is `name` (user-set + capped
 *        12 chars) — preserves pre-mesh / BLE-only lamp compatibility.
 *        `bdAddr` is a stable secondary identifier used for the
 *        disposition cross-reference (renaming a lamp keeps the same
 *        BD_ADDR, so dispositions don't orphan).
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
  // Canonical uppercase colon-hex BLE Device Address (e.g.
  // "AA:BB:CC:DD:EE:FF") captured from the BLE scan callback. Stable
  // for the entry's lifetime (BD_ADDR doesn't change on rename).
  // Used as the per-peer disposition key — see Config::setDisposition.
  // Empty string until the first BLE advert from this peer has been
  // processed (in practice every entry is BLE-sourced; this is
  // defensive).
  std::string bdAddr;
  bool hasMac = false;
  uint32_t lastSeenViaBleMs = 0;
  uint32_t lastSeenViaEspNowMs = 0;

  // Stamped on the FIRST addOrUpdateFromBle for this BD_ADDR. Stable for
  // the entry's lifetime — never overwritten on subsequent sightings.
  // Used by custom AnimatedBehavior::control() implementations to detect
  // peer arrivals as a 3-liner:
  //   if (peer.firstSeenMs >= lastTickMs_) { /* just arrived */ }
  // C++ equivalent of the legacy Python `await network.arrived()` idiom.
  uint32_t firstSeenMs = 0;

  bool acknowledged = false;  // SocialBehavior's per-name greeting state
  // Packed semver (major<<16|minor<<8|patch) — extracted from MSG_HELLO.
  // Only set via the ESP-NOW path; BLE adv doesn't carry it. Zero until
  // we've heard at least one HELLO from this peer.
  uint32_t firmwareVersion = 0;
  // OTA state from HELLO_TLV_OTA_STATE. 0/1/2 = idle/sending/receiving.
  // Only set via the ESP-NOW path (peers' BLE adv doesn't carry it).
  // Defaults to idle, matches the protocol's "no TLV present = idle"
  // semantics.
  uint8_t otaState = 0;
  // Protocol version byte from the HELLO frame header (`data[2]`). Used
  // by the OTA distributor to build OFFER/CHUNK/DONE at the peer's
  // version — see lamp_protocol's PROTOCOL_VERSION_EMIT doc block.
  // Zero until we've heard a HELLO; that just means "don't OTA them
  // yet, we don't know which protocol version they understand."
  uint8_t protocolVersion = 0;
  // The peer's `{type}-{channel}` identity from HELLO_TLV_FW_CHANNEL (e.g.
  // "standard-beta"). Empty until we've heard the TLV (older peers don't
  // emit it). The OTA distributor uses it to skip OFFERs at a peer of a
  // different lamp-type/channel; empty = "unknown → offer anyway and let the
  // receiver's silent-drop gate". 16 bytes + NUL, matching FW_CHANNEL_LEN.
  char fwChannel[17] = {0};
  // Most recent BLE-scan RSSI (dBm) reported by the NimBLE callback for
  // any adv from this peer. `getReachableViaBle()` returns its result
  // sorted by lastRssi descending so consumers (PersonalityEngine's
  // closest-peer tracking) can do `peers.front()` for the physically
  // nearest lamp. `getReachableViaEspNow()` does NOT sort — the cascade
  // path in ExpressionManager does its own sort after filtering self +
  // naming. -127 means "unknown" (no BLE adv seen yet, or stored before
  // the BLE scan callback updates it); sorts to the back.
  //
  // SOURCE OF TRUTH: written ONLY by addOrUpdateFromBle from the BLE
  // scan callback's getRSSI() reading. The ESP-NOW HELLO path
  // (addOrUpdateFromEspNow) used to also write here; that was removed
  // so RSSI has a single source — see proximity.hpp. Single-source
  // RSSI means PersonalityEngine's hysteresis comparison
  // (kRssiHysteresisDb) never crosses transports.
  int8_t lastRssi = -127;
};

/**
 * @brief Wisp presence cache populated from MSG_WISP_HELLO. Single global
 *        slot — the most recent hello wins. The brightness-floor check in
 *        ShowReceiver reads from this struct to decide whether an incoming
 *        brightness-override below kBrightnessOverrideMin is allowed (yes
 *        if a recent hello from the same MAC is on file).
 */
struct WispCache {
  bool present = false;
  uint8_t mac[6] = {0};
  uint32_t lastHelloMs = 0;
  uint32_t wispVersion = 0;
  uint8_t flags = 0;
  // +1 for trailing NUL so logging the string is safe; the on-wire slot
  // is 8 bytes opaque so we don't enforce ASCII.
  char paletteIdPrefix[9] = {0};
  char carriedFwChannel[9] = {0};
  uint32_t carriedFwVersion = 0;
  // Last wispStatus JSON broadcast for this wisp (verbatim
  // payload). Served on CHAR_WISP_STATUS reads merged with the hello
  // fields above. Empty until the first wispStatus has been seen.
  std::string lastStatusJson;
  uint32_t lastStatusMs = 0;
  // Latest MSG_WISP_PALETTE the lamp has heard from this wisp. The wisp
  // emits this alongside wispStatus every 30 s + on-change so the app's
  // wisp editor can read the canonical palette through any connected
  // lamp. Served base64-encoded as getWispStatusReadJson()'s `palette`
  // field, on the READ leg only (the NOTIFY leg omits it — MTU).
  // Capacity matches lamp_protocol::kMaxWispPaletteColors * 3 = 150 bytes.
  uint8_t manualPaletteRgb[150] = {0};
  uint8_t manualPaletteCount = 0;
  uint32_t lastPaletteMs = 0;
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
                             const char* fwChannel = nullptr);

  // Drop entries whose most-recent sighting (max of the two transports)
  // is older than `maxAgeMs`.
  void prune(uint32_t maxAgeMs);

  // SocialBehavior: only entries whose lastSeenViaBleMs is within maxAgeMs.
  std::vector<NearbyLamp> getReachableViaBle(uint32_t maxAgeMs);

  // Grid view / remote-config: only entries whose lastSeenViaEspNowMs
  // is within maxAgeMs.
  std::vector<NearbyLamp> getReachableViaEspNow(uint32_t maxAgeMs);

  // Full snapshot — used by CHAR_NEARBY_LAMPS for the app's unified list.
  std::vector<NearbyLamp> getAll();

  // Look up a peer by BD_ADDR (uppercase colon-hex, matches `NearbyLamp::
  // bdAddr`). Returns true and fills [out] on hit; false on miss with
  // [out] untouched. Snapshot-style copy under the same mutex pattern as
  // getAll(); the caller owns the copy.
  bool findByBdAddr(const std::string& bdAddr, NearbyLamp& out);

  // Look up a peer by ESP-NOW MAC. Returns true and fills [out] on hit;
  // false on miss with [out] untouched. Only matches entries whose
  // hasMac is true — BLE-only entries (HELLO never received) are
  // invisible here by design. Used by the OTA visual indicator to fetch
  // the active OTA peer's base color for the progress overlay.
  bool findByMac(const uint8_t mac[6], NearbyLamp& out);

  // Mark a lamp as acknowledged. SocialBehavior calls this once per peer
  // so a re-trigger doesn't re-greet the same lamp until it prunes.
  void acknowledge(const std::string& name);

  // Wisp presence — populated by the MSG_WISP_HELLO drain in the loop
  // task. Single global slot; most recent hello wins.
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
  // Loop-task-only writer (drain of pendingWispStatus on Core 1);
  // portMAX_DELAY take. If [mac] differs from the cached hello mac, the
  // cache mac is updated and `present` is asserted — a status broadcast
  // from a previously-unseen wisp is itself proof the wisp is on the
  // mesh, regardless of whether a hello has arrived yet.
  void cacheWispStatus(const uint8_t mac[6],
                       const char* json, size_t jsonLen);

  // Cache the latest MSG_WISP_PALETTE broadcast for a given wisp MAC.
  // Same single-slot semantics as cacheWispStatus — a different MAC
  // overwrites and clears stale per-wisp data. `rgb` is `count * 3`
  // bytes of packed R, G, B. `count` is clamped to the on-wire cap
  // (kMaxWispPaletteColors) so an oversized payload silently truncates
  // rather than overflowing the cache slot. Loop-task-only writer (drain
  // of pendingWispPalette on Core 1); portMAX_DELAY take.
  void cacheWispPalette(const uint8_t mac[6],
                        const uint8_t* rgb, uint8_t count);

  // Light-touch presence ping: assert that we received a wisp-sourced
  // paint frame from [mac]. Same single-slot semantics as cacheWispHello
  // (different mac → clear stale per-wisp data), but does NOT set
  // lastHelloMs or any hello-derived fields — we never received a hello
  // in this code path. The merge in getWispStatusReadJson() guards on
  // those fields' specific timestamps before publishing them.
  //
  // Why this exists: without it, a lamp that hears a wisp's
  // MSG_OVERRIDE_COLORS (unicast paint, no relay) but not its
  // MSG_WISP_HELLO (gossip-broadcast, ≤30s heartbeat) returns "{}" for
  // wispStatus until the next hello lands — the app shows "No wisp
  // detected" even though the lamp is being actively wisp-painted.
  // Observed on hardware as the "bytes=0 puzzle".
  //
  // Called from the loop-task drain of pendingOverrideColors on Core 1,
  // but uses bounded-take (2 ms) because the BLE on-read of
  // CHAR_WISP_STATUS runs on Core 0 and takes the same mutex — without
  // the bounded-take, a contended read could be starved by a
  // back-to-back paint-frame update. On timeout we drop the update;
  // the next paint frame retries.
  void cacheWispMacFromPaint(const uint8_t mac[6]);

  // Build and return the JSON to serve on CHAR_WISP_STATUS reads.
  // Merges the cached wispStatus payload with the last MSG_WISP_HELLO
  // data. Returns "{}" if nothing has been cached for either path.
  std::string getWispStatusReadJson(bool includePalette = false);

  // Snapshot of "is this lamp currently following a wisp on each
  // surface, and what's the most recently painted wisp color." The Flutter
  // app reads these through wispStatus to render the will-o'-wisp
  // indicator widget in the control screen header and to grey out
  // expressions that opt into `disabledDuringWispOverride`. RGB+W hex
  // color string; empty when the surface has never received a wisp
  // paint. Set via setLampWispStateProvider() so the depencency on the
  // ColorOverride globals stays in lamp.cpp.
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
