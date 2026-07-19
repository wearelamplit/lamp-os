#pragma once

#include <cstddef>
#include <cstdint>

#include "fleet/lamp_inventory.hpp"

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

// Per-peer claim-entry cap; the wire cap kMaxWispClaimEntries per
// MSG_WISP_CLAIM frame.
constexpr size_t WISP_ROSTER_MAX_PEER_ENTRIES = 32;

// Aging window: a peer wisp's claim entries are dropped from the shared
// view after this long of silence. 10 s = 5 × the broadcast cadence,
// short enough that a permanently-gone peer's lamps get adopted before
// the show stalls, long enough to survive 4 consecutive missed packets
// without spurious failover.
constexpr uint32_t WISP_ROSTER_PEER_AGE_MS = 10000;

// Hysteresis band on RSSI comparison. The wisp's RSSI must beat the best peer's
// RSSI by at least this many dB to flip a claim. Within the band, the
// current owner keeps it (no ping-pong on the natural ±5 dB jitter).
// Also the exit band below the range floor: an already-claimed lamp is
// retained down to floor - this many dB so boundary lamps don't flap.
constexpr int8_t WISP_ROSTER_HYSTERESIS_DB = 5;

/**
 * Wisp-side coordination for the multi-wisp claim system.
 *
 * Owns three pieces of state:
 *  - **Peer-claim view**: `peerWispMac → {lampMac → rssi}` map of every
 *    other wisp's claim broadcasts heard on the mesh, with a 10 s
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
  // Drops the update on mutex contention; the next broadcast (2 s) will
  // retry. Self-broadcasts (sourceMac == selfMac_) are ignored.
  void recordPeerClaim(const uint8_t peerWispMac[6],
                       const uint8_t* entries, uint8_t count,
                       uint32_t nowMs);

  // Recompute the own-claim set from the current peer view + the
  // provided lamp observations (LampInventory::copyObservations feed).
  // Ages out stale peers along the way. Runs on the claim-broadcast
  // cadence (2 s), matching where the result is consumed.
  //
  // `rangeFloorDbm` gates admission: a lamp joins the claim set only at
  // RSSI >= floor and is retained down to floor - WISP_ROSTER_HYSTERESIS_DB.
  //
  // The observations array is borrowed for the duration of the call;
  // the roster doesn't retain it.
  void recomputeClaims(const LampObservation* observations, size_t count,
                       uint32_t nowMs, int8_t rangeFloorDbm);

  // Predicate for `PaintDistributor::beginWalk()`: skip lamps where
  // this returns false. Cheap; takes the mutex briefly.
  bool claims(const uint8_t lampMac[6]) const;

  // Build one MSG_WISP_CLAIM frame's packed `(lampMac, rssi)` entries.
  // Composite frame: contested entries ride every frame (rotating among
  // themselves when they alone overflow it); remaining slots cycle the
  // rest of the claim set so lamp caches accumulate full coverage.
  // Each entry is 7 bytes (6 + 1); `outCapacity` is in bytes. Returns
  // the number of entries written (NOT the byte count). Caps at
  // WISP_ROSTER_MAX_PEER_ENTRIES. Advances the rotation cursors.
  size_t snapshotClaimsForBroadcast(uint8_t* outBuf, size_t outCapacity);

  // Store the paint pick for a currently-claimed lamp. No-op if mac is
  // not in the current own-claim set (the caller may have picked before
  // the claim was established).
  void setLampPaint(const uint8_t mac[6], const uint8_t base[3],
                    const uint8_t shade[3]);

  // Pack own-claim paint colors for `MSG_WISP_PAINT` broadcasting.
  // Each entry is lampMac(6)+base(3)+shade(3) = 12 bytes. Rotates a
  // WISP_PAINT_MAX_ENTRIES-wide window over the claim set per call
  // (2 s beacon tick); `cap` limits the output byte budget. Returns
  // the entry count written (not bytes).
  size_t snapshotPaintForBroadcast(uint8_t* out, size_t cap);

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
    uint8_t lampMac[6];
    int8_t  rssi;
    // Boundary lamp needing per-frame arbitration: a live peer also lists
    // it, or it sits in the range-floor exit band.
    bool    contested;
    uint8_t base[3];
    uint8_t shade[3];
  };
  OwnClaim ownClaims_[WISP_ROSTER_MAX_LAMPS];
  size_t   ownCount_ = 0;

  // Rotation cursors for the composite claim frame and the paint window.
  // Approximate positions over a set that recomputes every tick; exact
  // fairness isn't required, full coverage over a few frames is.
  size_t claimRotCursor_      = 0;
  size_t claimContestedCursor_ = 0;
  size_t paintCursor_         = 0;

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
