#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "components/network/protocol/lamp_protocol.hpp"
#include "util/bd_addr.hpp"
#include "util/color.hpp"
#include "wisp_cache.hpp"
#include "wisp_fleet_cache.hpp"

// Default prune age (milliseconds) shared between callers. NimBLE scan
// timer uses this when it runs onScanEnd; if a lamp hasn't been heard on
// EITHER transport in this window, it's dropped.
#ifndef LAMP_PRUNE_TIME_MS
#define LAMP_PRUNE_TIME_MS 240000
#endif

namespace lamp {

/**
 * One peer in the roster. Identity keys on `mac`, the stable social
 *        identity: the ESP-NOW/HELLO path stores it raw, the BLE-scan path
 *        recovers it from the scanned address. Dispositions, the greeting
 *        dedup, and the roster itself all key on it, so a rename doesn't
 *        orphan an entry and two same-named lamps don't collide. `name` is
 *        a stored display field (user-set, capped at HELLO_MAX_NAME).
 *
 *        Two facets, one per transport timestamp:
 *
 *        - near (`lastSeenNearMs`): physically close. Set by any BT
 *          sighting (short-range beacon, ungated) or by a direct ESP-NOW
 *          HELLO whose RSSI clears the mesh threshold (`markNear`). Gates
 *          greetings and crowd-dim.
 *        - mesh = seen via ESP-NOW (`lastSeenMeshMs`): network reachable.
 *          Gates OTA, remote config, and presence.
 *
 *        Trivially copyable by design: roster snapshots are frequent, and
 *        per-entry heap strings fragment the lamp's small heap.
 */
struct RosterEntry {
  char name[lamp_protocol::HELLO_MAX_NAME + 1] = {0};
  Color baseColor = Color();
  Color shadeColor = Color();
  // Mesh (WiFi-STA) MAC: raw from a HELLO, or recovered from a BLE
  // sighting's scanned address. The disposition-store and greeting key.
  uint8_t mac[6] = {0};
  bool hasMac = false;
  uint32_t lastSeenNearMs = 0;
  uint32_t lastSeenMeshMs = 0;
  bool acknowledged = false;  // SocialBehavior's per-MAC greeting state
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
  // Peer's HELLO_TLV_FW_MAX_CHUNK: the largest MSG_FW_CHUNK payload its OTA
  // receiver accepts. 0 on older peers or peers that never receive firmware
  // OTA; the distributor falls back to FW_CHUNK_SIZE_BASELINE for those.
  uint16_t maxChunk = 0;
  // Peer's `{type}-{channel}` slot from HELLO_TLV_FW_CHANNEL (e.g. "standard-beta").
  // Empty on older peers that predate the TLV; OTA distributor skips cross-type OFFERs.
  char fwChannel[lamp_protocol::HELLO_FW_CHANNEL_LEN + 1] = {0};
  // Peer's FS-image digest prefix from HELLO_TLV_FS_STATE.
  // hasFsDigest=false on older peers. The FS OTA distributor compares
  // this to the local digest to decide whether to offer the UI image.
  bool    hasFsDigest = false;
  uint8_t fsDigest[8] = {0};
  // Peer's HELLO_TLV_NEED_FS: empty/unmountable FS at this firmware version, so
  // it can't emit a digest. The FS distributor offers to it despite no digest.
  bool    needsFs = false;
  // Peer's HELLO_TLV_OTA_SENDING_TO: the mesh MAC it's OTA-distributing to.
  // hasOtaSendingTo=false unless the peer is mid-distribution. Surfaced in the
  // nearby JSON so the app can name the send edge and mark the silent receiver.
  bool    hasOtaSendingTo = false;
  uint8_t otaSendingTo[6] = {0};
  // BLE-scan RSSI (dBm). Written only by addOrUpdateFromBle (single-transport
  // invariant for PersonalityEngine hysteresis). -127 = unknown, sorts to back.
  // getNear() sorts descending; getMesh() does not.
  int8_t lastRssi = -127;
  // ESP-NOW HELLO RSSI (dBm), written only by addOrUpdateFromEspNow. Distinct
  // from lastRssi (BLE-scan RSSI): feeds the OTA distributor's signal-floor
  // gate. -127 = unknown, not gated.
  int8_t espnowRssi = -127;

  // Canonical uppercase colon-hex mac ("AA:BB:CC:DD:EE:FF"), the
  // disposition-store key format. Empty when hasMac is false.
  std::string macStr() const {
    if (!hasMac) return {};
    char buf[18];
    formatBdAddr(mac, buf);
    return buf;
  }
};

static_assert(std::is_trivially_copyable<RosterEntry>::value,
              "roster snapshots must not touch the heap per entry");

/**
 * Single source of truth for "lamps I can hear right now."
 *        Mutated from NimBLE scan callback (Core 0) and ESP-NOW HELLO
 *        recv (WiFi task); read from the loop task. SemaphoreHandle_t
 *        mutex serialises. Storage is a fixed static array; when full,
 *        the stalest entry (max of both last-seen stamps) is evicted.
 */
class LampRoster {
 public:
  // Max peers this lamp tracks. Distinct from WispFleetCache::kCapacity (the
  // broader wisp-claim set, which stays 100 so wisps claim all nearby lamps).
  static constexpr size_t kCapacity = 50;

  LampRoster();

  void addOrUpdateFromBle(const std::string& name,
                          const std::string& bleAddr,
                          const Color& base, const Color& shade,
                          int8_t rssi = -127);
  void addOrUpdateFromEspNow(const std::string& name, const uint8_t mac[6],
                             const Color& base, const Color& shade,
                             uint32_t firmwareVersion = 0,
                             uint8_t otaState = 0,
                             uint8_t protocolVersion = 0,
                             const char* fwChannel = nullptr,
                             const uint8_t* fsDigest = nullptr,
                             bool hasFsDigest = false,
                             uint16_t maxChunk = 0,
                             bool needsFs = false,
                             const uint8_t* otaSendingTo = nullptr,
                             bool hasOtaSendingTo = false,
                             int8_t rssi = -127);

  // Drop entries whose most-recent sighting (max of the two transports)
  // is older than `maxAgeMs`.
  void prune(uint32_t maxAgeMs);

  // Peers seen via BLE within maxAgeMs, sorted by RSSI descending
  // (front = physically closest).
  std::vector<RosterEntry> getNear(uint32_t maxAgeMs);

  // Highest-RSSI near arrival (hasMac, unacknowledged, seen within maxAgeMs)
  // that `accept` also passes, copied into `out`. Scans store_ in place under
  // the mutex, no snapshot and no sort, so the ~60 Hz greeting tick can't
  // fragment the heap. `accept` runs inside the critical section: it must not
  // allocate or block. Equal RSSI breaks on mac bytes ascending for a
  // deterministic pick. Returns false and leaves `out` untouched when nothing
  // qualifies.
  template <typename Accept>
  bool bestUngreetedArrival(uint32_t maxAgeMs, uint32_t now, Accept accept,
                            RosterEntry& out) {
    bool found = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < count_; i++) {
      const RosterEntry& e = store_[i];
      if (!e.hasMac) continue;
      if (e.lastSeenNearMs == 0) continue;
      if ((now - e.lastSeenNearMs) > maxAgeMs) continue;
      if (e.acknowledged) continue;
      if (!accept(e)) continue;
      if (!found || e.lastRssi > out.lastRssi ||
          (e.lastRssi == out.lastRssi && std::memcmp(e.mac, out.mac, 6) < 0)) {
        out = e;
        found = true;
      }
    }
    xSemaphoreGive(mutex_);
    return found;
  }

  // Peers seen via ESP-NOW within maxAgeMs (mesh-reachable; OTA and
  // remote-config candidates).
  std::vector<RosterEntry> getMesh(uint32_t maxAgeMs);

  // Full snapshot, used by CHAR_NEARBY_LAMPS for the app's unified list.
  std::vector<RosterEntry> getAll();

  // Monotonic counter, bumped on every entry mutation. Change detection
  // for the nearby-JSON cache rebuild in ble_control::tick().
  uint32_t generation();

  // Returns true and fills [out] on hit; false on miss. Only matches entries
  // with hasMac=true; BLE-only entries (no HELLO received) are excluded.
  // Used by the OTA progress overlay to fetch the active peer's color.
  bool findByMac(const uint8_t mac[6], RosterEntry& out);

  // Mark a lamp as acknowledged. SocialBehavior calls this once per peer
  // so a re-trigger doesn't re-greet the same lamp until it prunes.
  void acknowledge(const uint8_t mac[6]);

  // Set the near timestamp on an existing entry (direct-HELLO proximity).
  // No-op if the MAC is unknown.
  void markNear(const uint8_t mac[6]);

  // Cache wisp presence from MSG_WISP_HELLO. Display-slot admission: a
  // rival wisp is rejected while the current wisp's hellos are fresh
  // (kWispDisplayStaleMs). Returns true when this hello established a new
  // display-slot wisp (first sighting or a takeover), so the caller can
  // push a wispStatus notify for the presence edge rather than on every
  // 2 s keepalive.
  bool cacheWispHello(const uint8_t mac[6],
                      uint32_t wispVersion,
                      uint8_t flags,
                      const char* paletteIdPrefix,  // 8 bytes; not NUL-terminated
                      const char* carriedFwChannel, // 8 bytes; not NUL-terminated
                      uint32_t carriedFwVersion);

  // Cache the latest wispStatus JSON broadcast. Same display-slot
  // admission as cacheWispHello; a status from an unseen wisp asserts
  // presence even before a hello arrives. Loop-task-only writer (Core 1).
  void cacheWispStatus(const uint8_t mac[6],
                       const char* json, size_t jsonLen);

  // Cache the latest MSG_WISP_PALETTE. Same display-slot admission as
  // cacheWispHello. `rgbw` is `count * 4` interleaved R,G,B,W bytes;
  // `count` is clamped to kMaxWispPaletteColors. Loop-task-only writer
  // (Core 1).
  void cacheWispPalette(const uint8_t mac[6],
                        const uint8_t* rgbw, uint8_t count);

  // Accumulate one MSG_WISP_CLAIM frame into the fleet cache. A claim
  // frame from a rival wisp that names this lamp (`selfMac`) takes the
  // display slot when the current wisp does not claim it; otherwise
  // rival frames are rejected while the current wisp's hellos are fresh.
  // Loop-task-only writer (Core 1).
  void cacheWispClaim(const uint8_t mac[6],
                      const uint8_t lampMacs[][6], uint8_t count,
                      const uint8_t selfMac[6], uint32_t nowMs);

  // Accumulate one MSG_WISP_PAINT frame into the fleet cache. Same
  // display-slot admission as cacheWispHello. `entries` is
  // `count * WISP_PAINT_ENTRY_SIZE` bytes: lampMac(6)+baseRGB(3)+shadeRGB(3)
  // per entry. Loop-task-only writer (Core 1).
  void cacheWispPaint(const uint8_t srcMac[6],
                      const uint8_t* entries, uint8_t count,
                      uint32_t nowMs);

  // Build the binary blob served on CHAR_WISP_CLAIMS and the `wispclaims`
  // page section: [count:1][mac:6*count][colorPair:6*count], see
  // WispFleetCache::buildClaimsBlob. Entries beyond (outCap-1)/12 are
  // dropped, so a small direct-read buffer serves a truncated set while
  // the page section serves the full accumulation.
  size_t buildWispClaimsBlob(uint8_t* out, size_t outCap, uint32_t nowMs);

  // Record the source of an addressed wisp OVERRIDE_COLORS frame and assert
  // display presence when the slot is empty or stale so the first BLE
  // wispStatus read is non-empty. 2 ms bounded take: CHAR_WISP_STATUS reads
  // on Core 0 share this mutex.
  void cacheWispMacFromPaint(const uint8_t mac[6]);

  // True when `mac` is the wisp currently claiming this lamp (the fresh
  // display-slot wisp). Gates brightness overrides in every source mode.
  // 2 ms bounded take; false on timeout so the frame drops.
  bool isClaimingWisp(const uint8_t mac[6], uint32_t nowMs);

  // Copy the display-slot wisp's MAC into `out` when the slot is present and
  // fresh, returning true. False (out untouched) when empty, stale, or the
  // bounded take times out. The unicast target for the lamp's wisp control op.
  bool copyDisplayWispMac(uint8_t out[6], uint32_t nowMs);

  // Build and return the JSON to serve on CHAR_WISP_STATUS reads.
  // Merges the cached wispStatus payload with the last MSG_WISP_HELLO
  // data. Returns "{}" if nothing has been cached for either path.
  std::string getWispStatusReadJson();

  // Copy the cached manual palette (interleaved R,G,B,W per color) into
  // `out`, returning bytes written. Served on the `wisppalette` page
  // section, decoupled from the notify-clobbered status char. 0 on an
  // empty palette or a bounded-take timeout.
  size_t copyManualPaletteBlob(uint8_t* out, size_t cap);

  // Current wisp-control state per surface: whether each surface is
  // actively wisp-painted and the most recent paint color.
  // Set via setLampWispStateProvider() to keep ColorOverride coupling in lamp.cpp.
  struct LampWispState {
    bool        controllingBase = false;
    bool        controllingShade = false;
    std::string baseWispColor;   // colorToHexString "#rrggbbww", or empty
    std::string shadeWispColor;  // same
  };
  using LampWispStateProvider = LampWispState (*)();
  void setLampWispStateProvider(LampWispStateProvider provider) {
    lampWispStateProvider_ = provider;
  }

 private:
  LampWispStateProvider lampWispStateProvider_ = nullptr;
  std::array<RosterEntry, kCapacity> store_;
  size_t count_ = 0;
  uint32_t generation_ = 0;
  SemaphoreHandle_t mutex_ = nullptr;
  WispCache wispCache_;
  WispFleetCache wispFleet_;

  // Caller must hold the mutex. True when `mac` may take or keep the
  // display slot; adopts (clearing per-wisp state) when it may and the
  // slot held a different wisp.
  bool admitWispLocked(const uint8_t mac[6], uint32_t nowMs);
  // Caller must hold the mutex. Clears all per-wisp display state plus
  // the fleet cache, installs `mac`, and stamps slot freshness at `nowMs`.
  void adoptWispLocked(const uint8_t mac[6], uint32_t nowMs);

  // Caller must hold the mutex. Returns index of the entry keyed by `mac`,
  // or count_ if not found. Skips entries without a MAC so their all-zero
  // default doesn't collide.
  size_t findIndexLocked(const uint8_t mac[6]) const;
  // Caller must hold the mutex. Evicts the entry with the oldest combined
  // last-seen if the store is at capacity.
  void evictOldestIfFullLocked();
};

extern LampRoster lampRoster;  // single global instance, defined in .cpp

}  // namespace lamp
