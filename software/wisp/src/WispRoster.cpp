#include "WispRoster.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
// Native test build — stub the FreeRTOS surface to no-ops. Single-thread
// test harness has no concurrent access; the mutex acts solely as a
// sequence point on hardware, so dropping it for native is safe.
#include <cstddef>
typedef void* SemaphoreHandle_t;
#define pdTRUE         1
#define portMAX_DELAY  0xFFFFFFFFu
inline int pdMS_TO_TICKS(unsigned ms) { return static_cast<int>(ms); }
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  // Return a non-null sentinel so callers' null-checks succeed.
  return reinterpret_cast<SemaphoreHandle_t>(0x1);
}
inline int xSemaphoreTake(SemaphoreHandle_t, int) { return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
#endif

#include <climits>
#include <cstring>

namespace wisp {

namespace {
inline SemaphoreHandle_t asHandle(void* m) {
  return reinterpret_cast<SemaphoreHandle_t>(m);
}

// Packed wire entry size; mirrors WISP_CLAIM_ENTRY_SIZE in lamp_protocol.
constexpr size_t kEntryBytes = 6 + 1;
}  // namespace

WispRoster::WispRoster() {
  mutex_ = xSemaphoreCreateMutex();
}

WispRoster::~WispRoster() {
  if (mutex_) {
    vSemaphoreDelete(asHandle(mutex_));
    mutex_ = nullptr;
  }
}

void WispRoster::setSelfMac(const uint8_t mac[6]) {
  if (xSemaphoreTake(asHandle(mutex_), portMAX_DELAY) != pdTRUE) return;
  std::memcpy(selfMac_, mac, 6);
  selfMacSet_ = true;
  xSemaphoreGive(asHandle(mutex_));
}

void WispRoster::recordPeerClaim(const uint8_t peerWispMac[6],
                                 const uint8_t* entries, uint8_t count,
                                 uint32_t nowMs) {
  // Bounded take — runs on the WiFi recv task. Drop on contention; the
  // wisp's broadcast cadence (2 s) gives us 5× retries before peer aging
  // would treat us as silent.
  if (xSemaphoreTake(asHandle(mutex_), pdMS_TO_TICKS(2)) != pdTRUE) return;

  // Ignore self-broadcasts. (Should not happen in practice — wisps don't
  // hear their own ESP-NOW broadcasts — but defensive against a future
  // gossip-relay loopback.)
  if (selfMacSet_ && std::memcmp(peerWispMac, selfMac_, 6) == 0) {
    xSemaphoreGive(asHandle(mutex_));
    return;
  }

  size_t idx = findPeerLocked(peerWispMac);
  if (idx == peerCount_) {
    if (peerCount_ < WISP_ROSTER_MAX_PEERS) {
      idx = peerCount_++;
    } else {
      // Evict the peer with the OLDEST lastSeenMs — it's the most likely
      // to have gone silent, and we'd drop it on the next prune anyway.
      uint32_t oldest = peers_[0].lastSeenMs;
      idx = 0;
      for (size_t i = 1; i < peerCount_; ++i) {
        if (peers_[i].lastSeenMs < oldest) {
          oldest = peers_[i].lastSeenMs;
          idx = i;
        }
      }
    }
    std::memcpy(peers_[idx].mac, peerWispMac, 6);
  }

  peers_[idx].lastSeenMs = nowMs;
  const uint8_t clamped = count > WISP_ROSTER_MAX_LAMPS
                              ? WISP_ROSTER_MAX_LAMPS
                              : count;
  peers_[idx].count = clamped;
  for (uint8_t i = 0; i < clamped; ++i) {
    const uint8_t* src = entries + i * kEntryBytes;
    std::memcpy(peers_[idx].entries[i].lampMac, src, 6);
    peers_[idx].entries[i].rssi = static_cast<int8_t>(src[6]);
  }

  xSemaphoreGive(asHandle(mutex_));
}

void WispRoster::recomputeClaims(const LampObservation* observations,
                                 size_t count, uint32_t nowMs) {
  if (xSemaphoreTake(asHandle(mutex_), portMAX_DELAY) != pdTRUE) return;

  // Aging first: any peer silent for > WISP_ROSTER_PEER_AGE_MS gets
  // dropped from the shared view. This makes failover implicit — when
  // a peer goes offline, its entries vanish and the survivor's claim
  // logic naturally adopts the orphans on the next tick.
  prunePeersLocked(nowMs);

  // Build a fresh own-claim set. We don't mutate ownClaims_ in place
  // because the iteration order over `observations` differs from the
  // existing ownClaims_ order, and we need to remember the previous
  // ownership for hysteresis.
  OwnClaim next[WISP_ROSTER_MAX_LAMPS];
  size_t   nextCount = 0;

  const size_t cap = count > WISP_ROSTER_MAX_LAMPS
                         ? WISP_ROSTER_MAX_LAMPS
                         : count;
  for (size_t i = 0; i < cap; ++i) {
    const LampObservation& obs = observations[i];
    // No RSSI yet → don't claim. The lamp will get re-evaluated on
    // the next tick once a real measurement lands.
    if (obs.rssi == INT8_MIN) continue;

    uint8_t bestPeerMac[6] = {0};
    const int8_t bestPeerRssi = bestPeerRssiLocked(obs.mac, bestPeerMac);

    // No peer claims this lamp → we claim it unconditionally.
    if (bestPeerRssi == INT8_MIN) {
      std::memcpy(next[nextCount].lampMac, obs.mac, 6);
      next[nextCount].rssi = obs.rssi;
      nextCount++;
      continue;
    }

    const int advantage = static_cast<int>(obs.rssi) -
                          static_cast<int>(bestPeerRssi);

    if (advantage >= WISP_ROSTER_HYSTERESIS_DB) {
      // Strictly closer → claim it.
      std::memcpy(next[nextCount].lampMac, obs.mac, 6);
      next[nextCount].rssi = obs.rssi;
      nextCount++;
      continue;
    }

    if (advantage <= -WISP_ROSTER_HYSTERESIS_DB) {
      // Strictly further → don't claim it.
      continue;
    }

    // Within the ±5 dB hysteresis band. Stickiness rule: whoever
    // claimed it last tick keeps it. If both we AND a peer are
    // currently claiming it (simultaneous-claim window from packet
    // loss), the lower MAC wins.
    const bool weClaimedLast = findOwnLocked(obs.mac) != ownCount_;
    if (weClaimedLast) {
      // Defend our claim unless the peer is also claiming AND has the
      // lower MAC.
      if (selfWinsTiebreakLocked(bestPeerMac)) {
        std::memcpy(next[nextCount].lampMac, obs.mac, 6);
        next[nextCount].rssi = obs.rssi;
        nextCount++;
      }
      // else: peer's MAC is lower and we're within hysteresis → yield.
      continue;
    }
    // We did NOT claim it last tick; a peer did. Hysteresis says keep
    // the current owner; we don't claim.
  }

  // Commit.
  std::memcpy(ownClaims_, next, sizeof(OwnClaim) * nextCount);
  ownCount_ = nextCount;

  xSemaphoreGive(asHandle(mutex_));
}

bool WispRoster::claims(const uint8_t lampMac[6]) const {
  if (xSemaphoreTake(asHandle(mutex_), pdMS_TO_TICKS(2)) != pdTRUE) {
    // Contention default: claim it. False would silently mute a
    // lamp's paint; missing the filter on contention is safer than
    // missing the paint.
    return true;
  }
  const bool result = const_cast<WispRoster*>(this)->findOwnLocked(lampMac) !=
                      ownCount_;
  xSemaphoreGive(asHandle(mutex_));
  return result;
}

size_t WispRoster::snapshotClaimsForBroadcast(uint8_t* outBuf,
                                              size_t outCapacity) const {
  if (!outBuf) return 0;
  if (xSemaphoreTake(asHandle(mutex_), portMAX_DELAY) != pdTRUE) return 0;
  const size_t maxEntries = outCapacity / kEntryBytes;
  const size_t n = ownCount_ < maxEntries ? ownCount_ : maxEntries;
  for (size_t i = 0; i < n; ++i) {
    uint8_t* dst = outBuf + i * kEntryBytes;
    std::memcpy(dst, ownClaims_[i].lampMac, 6);
    dst[6] = static_cast<uint8_t>(ownClaims_[i].rssi);
  }
  xSemaphoreGive(asHandle(mutex_));
  return n;
}

size_t WispRoster::claimedCount() const {
  if (xSemaphoreTake(asHandle(mutex_), portMAX_DELAY) != pdTRUE) return 0;
  const size_t n = ownCount_;
  xSemaphoreGive(asHandle(mutex_));
  return n;
}

size_t WispRoster::peerCount() const {
  if (xSemaphoreTake(asHandle(mutex_), portMAX_DELAY) != pdTRUE) return 0;
  const size_t n = peerCount_;
  xSemaphoreGive(asHandle(mutex_));
  return n;
}

// --- private helpers (all assume mutex held) ---

void WispRoster::prunePeersLocked(uint32_t nowMs) {
  for (size_t i = 0; i < peerCount_; ) {
    const uint32_t last = peers_[i].lastSeenMs;
    const uint32_t age  = (nowMs >= last) ? nowMs - last : 0;
    if (age > WISP_ROSTER_PEER_AGE_MS) {
      // Swap-with-back-and-pop.
      if (i != peerCount_ - 1) {
        peers_[i] = peers_[peerCount_ - 1];
      }
      peerCount_--;
      continue;
    }
    i++;
  }
}

size_t WispRoster::findPeerLocked(const uint8_t mac[6]) const {
  for (size_t i = 0; i < peerCount_; ++i) {
    if (std::memcmp(peers_[i].mac, mac, 6) == 0) return i;
  }
  return peerCount_;
}

size_t WispRoster::findOwnLocked(const uint8_t mac[6]) const {
  for (size_t i = 0; i < ownCount_; ++i) {
    if (std::memcmp(ownClaims_[i].lampMac, mac, 6) == 0) return i;
  }
  return ownCount_;
}

int8_t WispRoster::bestPeerRssiLocked(const uint8_t lampMac[6],
                                      uint8_t peerWispMacOut[6]) const {
  int8_t best = INT8_MIN;
  for (size_t p = 0; p < peerCount_; ++p) {
    const PeerWisp& peer = peers_[p];
    for (uint8_t e = 0; e < peer.count; ++e) {
      if (std::memcmp(peer.entries[e].lampMac, lampMac, 6) != 0) continue;
      const int8_t r = peer.entries[e].rssi;
      if (r != INT8_MIN && r > best) {
        best = r;
        std::memcpy(peerWispMacOut, peer.mac, 6);
      }
      break;  // a peer has at most one entry per lamp
    }
  }
  return best;
}

bool WispRoster::selfWinsTiebreakLocked(const uint8_t peerMac[6]) const {
  if (!selfMacSet_) return false;
  // Lexicographic byte comparison. Lower MAC wins.
  return std::memcmp(selfMac_, peerMac, 6) < 0;
}

}  // namespace wisp
