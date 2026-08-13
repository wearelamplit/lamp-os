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
  // wins the advantage/hysteresis arbitration.
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

  // Store the paint pick for a currently-claimed lamp. No-op if mac is
  // not in the current own-claim set (the caller may have picked before
  // the claim was established).
  void setLampPaint(const uint8_t mac[6], const uint8_t base[3],
                    const uint8_t shade[3]);

  // ponytail: orphaned — MSG_WISP_PAINT broadcast retired, STATE carries paint
  // via snapshotStateForBroadcast. Kept with its roster test; drop in cleanup.
  // Packs the own-claim paint set as lampMac(6)+base(3)+shade(3) = 12-byte
  // entries; `cap` limits the byte budget. Returns entry count written.
  size_t snapshotPaintForBroadcast(uint8_t* out, size_t cap);

  // Pack the whole own-claim set for `MSG_WISP_STATE` broadcasting: same
  // 12-byte lampMac+base+shade entries as the paint snapshot, but every
  // claimed lamp in one call with no rotation. STATE carries the full fleet
  // in a single frame; `cap` bounds the byte budget, entries past it drop.
  // Returns the entry count written (not bytes).
  size_t snapshotStateForBroadcast(uint8_t* out, size_t cap);

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
    uint8_t base[3];
    uint8_t shade[3];
  };
  OwnClaim ownClaims_[WISP_ROSTER_MAX_LAMPS];
  size_t   ownCount_ = 0;

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
