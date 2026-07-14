#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

// Cap on the number of peer wisps tracked at once. The design assumption
// is a small fleet (~2-4 wisps in shared range); 8 gives 2× headroom.
// LRU evict on overflow so a transient mesh-network glitch doesn't lock
// us into stale entries permanently.
constexpr size_t WISP_ROSTER_MAX_PEERS = 8;

// Cap on the number of lamps per peer's claim list and our own claim
// list. Matches LampInventory::MAX_LAMPS so a wisp can never claim more
// lamps than it has in inventory; also the wire-level kMaxWispClaimEntries cap.
constexpr size_t WISP_ROSTER_MAX_LAMPS = 32;

// Aging window: a peer wisp's claim entries are dropped from the shared
// view after this long of silence. 10 s = 5 × the broadcast cadence —
// short enough that a permanently-gone peer's lamps get adopted before
// the show stalls, long enough to survive 4 consecutive missed packets
// without spurious failover.
constexpr uint32_t WISP_ROSTER_PEER_AGE_MS = 10000;

// Hysteresis band on RSSI comparison. Our RSSI must beat the best peer's
// RSSI by at least this many dB to flip a claim. Within the band, the
// current owner keeps it (no ping-pong on the natural ±5 dB jitter).
constexpr int8_t WISP_ROSTER_HYSTERESIS_DB = 5;

/**
 * @brief Wisp-side coordination for the multi-wisp claim system.
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

  // Per-lamp observation feed for the tick computation. Called once
  // per lamp the wisp currently hears, before `recomputeClaims`.
  struct LampObservation {
    uint8_t mac[6];
    int8_t rssi;
  };

  // Recompute the own-claim set from the current peer view + the
  // provided lamp observations. Ages out stale peers along the way.
  // Should run before each tick where the result is consumed
  // (PaintDistributor walk + PresenceBeacon broadcast build).
  //
  // The observations array is borrowed for the duration of the call;
  // the roster doesn't retain it.
  void recomputeClaims(const LampObservation* observations, size_t count,
                       uint32_t nowMs);

  // Predicate for `PaintDistributor::beginWalk()`: skip lamps where
  // this returns false. Cheap; takes the mutex briefly.
  bool claims(const uint8_t lampMac[6]) const;

  // Snapshot the own-claim set as packed `(lampMac, rssi)` bytes for
  // `MSG_WISP_CLAIM` broadcasting. Writes up to `outCapacity` bytes;
  // each entry is 7 bytes (6 + 1). Returns the number of entries
  // written (NOT the byte count). Caps at WISP_ROSTER_MAX_LAMPS.
  size_t snapshotClaimsForBroadcast(uint8_t* outBuf, size_t outCapacity) const;

  // Store the paint pick for a currently-claimed lamp. No-op if mac is
  // not in the current own-claim set (the caller may have picked before
  // the claim was established).
  void setLampPaint(const uint8_t mac[6], const uint8_t base[3],
                    const uint8_t shade[3]);

  // Pack own-claim paint colors for `MSG_WISP_PAINT` broadcasting.
  // Each entry is lampMac(6)+base(3)+shade(3) = 12 bytes. Capped at
  // WISP_PAINT_MAX_ENTRIES; `cap` limits the output byte budget.
  // Returns the entry count written (not bytes).
  size_t snapshotPaintForBroadcast(uint8_t* out, size_t cap) const;

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
    PeerEntry entries[WISP_ROSTER_MAX_LAMPS];
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
