#include "fleet/wisp_roster.hpp"
#include "wire/lamp_protocol.hpp"

#include "fleet/freertos_shim.hpp"

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

static_assert(WISP_ROSTER_MAX_PEER_ENTRIES ==
                  lamp_protocol::kMaxWispClaimEntries,
              "peer claim storage must match the wire frame cap");

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
  // Bounded take, runs on the WiFi recv task. Drop on contention; the
  // wisp's broadcast cadence (2 s) gives 5× retries before peer aging
  // treats the wisp as silent.
  if (xSemaphoreTake(asHandle(mutex_), pdMS_TO_TICKS(2)) != pdTRUE) return;

  // Ignore self-broadcasts. Wisps don't hear their own ESP-NOW frames, but
  // the check guards against a gossip-relay loopback.
  if (selfMacSet_ && std::memcmp(peerWispMac, selfMac_, 6) == 0) {
    xSemaphoreGive(asHandle(mutex_));
    return;
  }

  size_t idx = findPeerLocked(peerWispMac);
  if (idx == peerCount_) {
    if (peerCount_ < WISP_ROSTER_MAX_PEERS) {
      idx = peerCount_++;
    } else {
      // Evict the peer with the OLDEST lastSeenMs; it's the most likely
      // to have gone silent, and the next prune would drop it anyway.
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
  const uint8_t clamped = count > WISP_ROSTER_MAX_PEER_ENTRIES
                              ? WISP_ROSTER_MAX_PEER_ENTRIES
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
                                 size_t count, uint32_t nowMs,
                                 int8_t rangeFloorDbm) {
  if (xSemaphoreTake(asHandle(mutex_), portMAX_DELAY) != pdTRUE) return;

  // Aging first: any peer silent for > WISP_ROSTER_PEER_AGE_MS gets
  // dropped from the shared view. This makes failover implicit. When
  // a peer goes offline, its entries vanish and the survivor's claim
  // logic naturally adopts the orphans on the next tick.
  prunePeersLocked(nowMs);

  // Build a fresh own-claim set rather than mutating ownClaims_ in place.
  // The iteration order over `observations` differs from the existing
  // ownClaims_ order, and the previous ownership must be remembered for
  // hysteresis.
  OwnClaim next[WISP_ROSTER_MAX_LAMPS];
  size_t   nextCount = 0;

  // Carries prior paint colors forward; zeroes them for a fresh claim.
  auto appendClaim = [&](const LampObservation& o, bool contested,
                         size_t prevIdx) {
    OwnClaim& c = next[nextCount++];
    std::memcpy(c.lampMac, o.mac, 6);
    c.rssi = o.rssi;
    c.contested = contested;
    if (prevIdx != ownCount_) {
      std::memcpy(c.base,  ownClaims_[prevIdx].base,  3);
      std::memcpy(c.shade, ownClaims_[prevIdx].shade, 3);
    } else {
      std::memset(c.base,  0, 3);
      std::memset(c.shade, 0, 3);
    }
  };

  const size_t cap = count > WISP_ROSTER_MAX_LAMPS
                         ? WISP_ROSTER_MAX_LAMPS
                         : count;
  for (size_t i = 0; i < cap; ++i) {
    const LampObservation& obs = observations[i];
    // No RSSI yet → don't claim. The lamp will get re-evaluated on
    // the next tick once a real measurement lands.
    if (obs.rssi == INT8_MIN) continue;

    // Range floor: admit at >= floor; an existing claim rides the exit
    // band down to floor - hysteresis before it drops.
    const size_t prevIdx = findOwnLocked(obs.mac);
    const bool weClaimedLast = prevIdx != ownCount_;
    const int8_t admitFloor =
        weClaimedLast
            ? static_cast<int8_t>(rangeFloorDbm - WISP_ROSTER_HYSTERESIS_DB)
            : rangeFloorDbm;
    if (obs.rssi < admitFloor) continue;
    const bool inExitBand = obs.rssi < rangeFloorDbm;

    uint8_t bestPeerMac[6] = {0};
    const int8_t bestPeerRssi = bestPeerRssiLocked(obs.mac, bestPeerMac);
    const bool contested = bestPeerRssi != INT8_MIN || inExitBand;

    // No peer claims this lamp → claim it unconditionally.
    if (bestPeerRssi == INT8_MIN) {
      appendClaim(obs, contested, prevIdx);
      continue;
    }

    const int advantage = static_cast<int>(obs.rssi) -
                          static_cast<int>(bestPeerRssi);

    if (advantage >= WISP_ROSTER_HYSTERESIS_DB) {
      appendClaim(obs, contested, prevIdx);
      continue;
    }

    if (advantage <= -WISP_ROSTER_HYSTERESIS_DB) {
      // Strictly further → don't claim it.
      continue;
    }

    // Within the ±5 dB hysteresis band. Stickiness rule: whoever
    // claimed it last tick keeps it. If both this wisp AND a peer are
    // currently claiming it (simultaneous-claim window from packet
    // loss), the lower MAC wins.
    if (weClaimedLast) {
      // Defend the claim unless the peer is also claiming AND has the
      // lower MAC.
      if (selfWinsTiebreakLocked(bestPeerMac)) {
        appendClaim(obs, contested, prevIdx);
      }
      // else: peer's MAC is lower and within hysteresis → yield.
      continue;
    }
    // This wisp did NOT claim it last tick; a peer did. Hysteresis keeps
    // the current owner; no claim here.
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
                                              size_t outCapacity) {
  if (!outBuf) return 0;
  if (xSemaphoreTake(asHandle(mutex_), portMAX_DELAY) != pdTRUE) return 0;

  const size_t byBuf = outCapacity / kEntryBytes;
  const size_t frameCap = byBuf < WISP_ROSTER_MAX_PEER_ENTRIES
                              ? byBuf
                              : WISP_ROSTER_MAX_PEER_ENTRIES;

  size_t contestedIdx[WISP_ROSTER_MAX_LAMPS];
  size_t restIdx[WISP_ROSTER_MAX_LAMPS];
  size_t contestedCount = 0;
  size_t restCount = 0;
  for (size_t i = 0; i < ownCount_; ++i) {
    if (ownClaims_[i].contested) {
      contestedIdx[contestedCount++] = i;
    } else {
      restIdx[restCount++] = i;
    }
  }

  size_t picked[WISP_ROSTER_MAX_PEER_ENTRIES];
  size_t n = 0;
  if (contestedCount >= frameCap) {
    // Contested alone overflow the frame: rotate among them so every
    // boundary lamp re-broadcasts within a few ticks. Display coverage
    // waits; arbitration degrades last.
    for (size_t i = 0; i < frameCap; ++i) {
      picked[n++] = contestedIdx[(claimContestedCursor_ + i) % contestedCount];
    }
    claimContestedCursor_ =
        (claimContestedCursor_ + frameCap) % contestedCount;
  } else {
    for (size_t i = 0; i < contestedCount; ++i) picked[n++] = contestedIdx[i];
    const size_t rotSlots = frameCap - contestedCount;
    const size_t take = rotSlots < restCount ? rotSlots : restCount;
    for (size_t i = 0; i < take; ++i) {
      picked[n++] = restIdx[(claimRotCursor_ + i) % restCount];
    }
    if (restCount) claimRotCursor_ = (claimRotCursor_ + take) % restCount;
  }

  for (size_t i = 0; i < n; ++i) {
    uint8_t* dst = outBuf + i * kEntryBytes;
    std::memcpy(dst, ownClaims_[picked[i]].lampMac, 6);
    dst[6] = static_cast<uint8_t>(ownClaims_[picked[i]].rssi);
  }
  xSemaphoreGive(asHandle(mutex_));
  return n;
}

void WispRoster::setLampPaint(const uint8_t mac[6], const uint8_t base[3],
                              const uint8_t shade[3]) {
  if (xSemaphoreTake(asHandle(mutex_), portMAX_DELAY) != pdTRUE) return;
  const size_t idx = findOwnLocked(mac);
  if (idx != ownCount_) {
    std::memcpy(ownClaims_[idx].base,  base,  3);
    std::memcpy(ownClaims_[idx].shade, shade, 3);
  }
  xSemaphoreGive(asHandle(mutex_));
}

size_t WispRoster::snapshotPaintForBroadcast(uint8_t* out, size_t cap) {
  if (!out) return 0;
  if (xSemaphoreTake(asHandle(mutex_), portMAX_DELAY) != pdTRUE) return 0;
  constexpr size_t kPaintEntry = 12;  // mac(6) + base(3) + shade(3)
  const size_t maxByBuf = cap / kPaintEntry;
  const size_t maxEntries = lamp_protocol::WISP_PAINT_MAX_ENTRIES < maxByBuf
                                ? lamp_protocol::WISP_PAINT_MAX_ENTRIES
                                : maxByBuf;
  const size_t n = ownCount_ < maxEntries ? ownCount_ : maxEntries;
  const size_t start = ownCount_ ? paintCursor_ % ownCount_ : 0;
  for (size_t i = 0; i < n; ++i) {
    const OwnClaim& c = ownClaims_[(start + i) % ownCount_];
    uint8_t* dst = out + i * kPaintEntry;
    std::memcpy(dst,     c.lampMac, 6);
    std::memcpy(dst + 6, c.base,    3);
    std::memcpy(dst + 9, c.shade,   3);
  }
  if (ownCount_) paintCursor_ = (start + n) % ownCount_;
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
