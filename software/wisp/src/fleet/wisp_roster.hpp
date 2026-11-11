#pragma once

#include <cstddef>
#include <cstdint>

#include "fleet/lamp_inventory.hpp"
#include "wire/lamp_protocol.hpp"

namespace wisp {

// Cap on the number of peer wisps tracked at once. The design assumption
// is a small fleet (~2-4 wisps in shared range); 8 gives 2× headroom.
// LRU evict on overflow so a transient mesh-network glitch doesn't lock
// the roster into stale entries permanently.
constexpr size_t WISP_ROSTER_MAX_PEERS = 8;

// Own-claim capacity. Matches LampInventory::MAX_LAMPS so the wisp can
// claim (and paint) its full inventory.
constexpr size_t WISP_ROSTER_MAX_LAMPS = 100;
static_assert(WISP_ROSTER_MAX_LAMPS == LampInventory::MAX_LAMPS,
              "claim set must cover the full inventory");
static_assert(WISP_ROSTER_MAX_LAMPS <= lamp_protocol::WISP_STATE_MAX_ENTRIES,
              "every claim must fit one MSG_WISP_STATE frame");

// Per-peer claim-entry cap; the wire cap kMaxWispClaimEntries per
// MSG_WISP_CLAIM frame.
constexpr size_t WISP_ROSTER_MAX_PEER_ENTRIES = 100;

// Aging window: a peer wisp's claim entries are dropped from the shared
// view after this long of silence. 20 s = 5 × the 4 s CLAIM cadence,
// short enough that a permanently-gone peer's lamps get adopted before
// the show stalls, long enough to tolerate ~5 missed CLAIMs (CLAIM has no
// resend) without spurious failover.
constexpr uint32_t WISP_ROSTER_PEER_AGE_MS = 20000;

// Hysteresis band on RSSI comparison. The wisp's RSSI must beat the best peer's
// RSSI by at least this many dB to flip a claim. Within the band, the
// current owner keeps it (no ping-pong on the natural ±5 dB jitter).
constexpr int8_t WISP_ROSTER_HYSTERESIS_DB = 5;

/**
 * Wisp-side coordination for the multi-wisp claim system.
 *
 * Owns three pieces of state:
 *  - **Peer-claim view**: `peerWispMac → {lampMac → rssi}` map of every
 *    other wisp's claim broadcasts heard on the mesh, with a 20 s
 *    aging window. Updated on every received `MSG_WISP_CLAIM`.
 *  - **Own claim set**: `lampMac → ourRssi` snapshot of the lamps this
 *    wisp currently believes it should paint. Recomputed every tick
 *    from the peer view + own LampInventory.
 *  - **Self MAC**: for the lower-MAC tiebreaker in rare conflicts.
 *
 * Threading: `recordPeerClaim` is called from the WiFi recv task; the
 * rest from the loop task. A single FreeRTOS mutex (bounded-take) guards
 * everything. Native builds use the same mutex helper as `LampInventory`.
 */
class WispRoster {
 public:
  WispRoster();
  ~WispRoster();

  // One-time wiring at boot. After this, the roster knows its own MAC
  // for tiebreak comparisons. Idempotent.
  void setSelfMac(const uint8_t mac[6]);

  // Called from the WiFi recv task when MSG_WISP_CLAIM arrives. Stores
  // the peer's claimed lamp/rssi entries and refreshes its `lastSeenMs`.
  // Drops the update on mutex contention; the next CLAIM broadcast (4 s)
  // will retry. Self-broadcasts (sourceMac == selfMac_) are ignored.
  void recordPeerClaim(const uint8_t peerWispMac[6],
                       const uint8_t* entries, uint8_t count,
                       uint32_t nowMs);

  // Recompute the own-claim set from the current peer view + the
  // provided lamp observations (LampInventory::copyObservations feed).
  // Ages out stale peers along the way. Runs on the 2 s tick; the CLAIM
  // broadcast rides the 4 s odd-tick sub-multiple. Claims every lamp this
  // wisp directly hears, deferring to a peer only when the peer's RSSI
  // wins the advantage/hysteresis arbitration. A claimed lamp whose direct
  // RSSI has aged out (INT8_MIN) stays claimed only while no peer reports a
  // direct RSSI for it; a lamp never heard directly is not claimed.
  //
  // The observations array is borrowed for the duration of the call;
  // the roster doesn't retain it.
  void recomputeClaims(const LampObservation* observations, size_t count,
                       uint32_t nowMs);

  // True when this wisp currently claims `lampMac`. PaintDistributor colors
  // only claimed lamps. Cheap; takes the mutex briefly.
  bool claims(const uint8_t lampMac[6]) const;

  // Pack the whole own-claim set into one MSG_WISP_CLAIM frame's `(lampMac,
  // rssi)` entries. Each entry is 7 bytes (6 + 1); `outCapacity` is in bytes.
  // Returns the number of entries written (NOT the byte count); truncates to
  // whatever `outCapacity` holds.
  size_t snapshotClaimsForBroadcast(uint8_t* outBuf, size_t outCapacity);

  // Store the RGBW paint pick for a currently-claimed lamp and mark it painted.
  // It shows at once, ending any drift fade in progress. No-op if mac is not
  // in the current own-claim set (the caller may have picked before the claim
  // was established).
  void setLampPaint(const uint8_t mac[6], const uint8_t base[4],
                    const uint8_t shade[4]);

  // Fade a claimed lamp's paint to `base` / `shade` over `fadeMs` from
  // `nowMs`, starting from the colors it shows at `nowMs`. A claim with no
  // paint yet, or fadeMs 0, takes the colors at once. No-op for an unclaimed
  // mac.
  void setLampDrift(const uint8_t mac[6], const uint8_t base[4],
                    const uint8_t shade[4], uint32_t nowMs, uint32_t fadeMs);

  // Pack every painted claim for `MSG_WISP_STATE` broadcasting: 12-byte
  // lampMac+baseRGB+shadeRGB entries into `out`, and the matching baseW,shadeW
  // pairs into `w`, each claim's colors as they show at `nowMs`. A claim with
  // no paint assigned yet is left out so the lamp holds its own scene; any
  // assigned color, black included, is packed. `cap` / `wCap` bound the byte
  // budgets, entries past either drop. Returns the entry count written (not
  // bytes).
  size_t snapshotStateForBroadcast(uint8_t* out, size_t cap, uint8_t* w,
                                   size_t wCap, uint32_t nowMs);

  // Diagnostics. Both take the mutex briefly.
  size_t claimedCount() const;
  size_t peerCount() const;

 private:
  uint8_t selfMac_[6] = {0};
  bool selfMacSet_ = false;

  struct PeerEntry {
    uint8_t lampMac[6];
    int8_t  rssi;
  };

  struct PeerWisp {
    uint8_t  mac[6];
    uint32_t lastSeenMs;
    uint8_t  count;
    PeerEntry entries[WISP_ROSTER_MAX_PEER_ENTRIES];
  };

  PeerWisp peers_[WISP_ROSTER_MAX_PEERS];
  size_t   peerCount_ = 0;

  struct OwnClaim {
    uint8_t  lampMac[6];
    int8_t   rssi;
    uint8_t  base[4];   // RGBW
    uint8_t  shade[4];  // RGBW
    bool     painted;
    uint8_t  fromBase[4];   // RGBW a drift fade starts from
    uint8_t  fromShade[4];
    uint32_t fadeStartMs;
    uint32_t fadeMs;        // 0: base/shade show as stored

    void shownAt(uint32_t nowMs, uint8_t outBase[4], uint8_t outShade[4]) const;
  };
  OwnClaim ownClaims_[WISP_ROSTER_MAX_LAMPS];
  size_t   ownCount_ = 0;
  // recomputeClaims' working set; a member so it stays off the 8 KB loop stack.
  OwnClaim nextClaims_[WISP_ROSTER_MAX_LAMPS];

  // Opaque; keeps FreeRTOS out of the header.
  void* mutex_ = nullptr;

  // All these helpers assume the caller holds the mutex.
  void   prunePeersLocked(uint32_t nowMs);
  size_t findPeerLocked(const uint8_t mac[6]) const;
  size_t findOwnLocked(const uint8_t mac[6]) const;
  // Best peer RSSI for `lampMac` across all live peers. Writes the
  // peer's wisp-MAC to `peerWispMacOut` when found. Returns
  // INT8_MIN if no peer claims the lamp.
  int8_t bestPeerRssiLocked(const uint8_t lampMac[6],
                            uint8_t peerWispMacOut[6]) const;
  // Returns true if `selfMac_` should win a tiebreak against `peerMac`.
  // Lower MAC wins (lexicographic comparison).
  bool   selfWinsTiebreakLocked(const uint8_t peerMac[6]) const;
};

}  // namespace wisp
