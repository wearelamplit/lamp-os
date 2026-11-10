#include "peer_backoff_ring.hpp"

#include <cstring>

#include <lampos/clock.hpp>

namespace lamp {

bool PeerBackoffRing::inBackoff(const uint8_t mac[6], uint32_t nowMs) const {
  for (const Entry& e : entries_) {
    if (!e.used || std::memcmp(e.mac, mac, 6) != 0) continue;
    if (e.persistent) return true;
    if (lampos::msSince(nowMs, e.sinceMs) < e.durationMs) return true;
  }
  return false;
}

void PeerBackoffRing::retire(uint32_t nowMs) {
  for (Entry& e : entries_) {
    if (e.used && !e.persistent) e.sinceMs = lampos::retireStamp(nowMs, e.sinceMs);
  }
}

void PeerBackoffRing::note(const uint8_t mac[6], uint32_t nowMs,
                           uint32_t durationMs, bool persistent) {
  for (Entry& e : entries_) {
    if (e.used && std::memcmp(e.mac, mac, 6) == 0) {
      e.sinceMs = nowMs;
      e.durationMs = durationMs;
      e.persistent = persistent;
      return;
    }
  }

  int freeSlot = -1, expiredTransient = -1, activeTransient = -1, oldestPersistent = -1;
  for (size_t i = 0; i < kSize; ++i) {
    const Entry& e = entries_[i];
    if (!e.used) {
      if (freeSlot < 0) freeSlot = static_cast<int>(i);
    } else if (!e.persistent) {
      if (lampos::msSince(nowMs, e.sinceMs) >= e.durationMs) {
        if (expiredTransient < 0) expiredTransient = static_cast<int>(i);
      } else if (activeTransient < 0) {
        activeTransient = static_cast<int>(i);
      }
    } else if (oldestPersistent < 0 ||
               lampos::msSince(nowMs, e.sinceMs) >
                   lampos::msSince(nowMs, entries_[oldestPersistent].sinceMs)) {
      oldestPersistent = static_cast<int>(i);
    }
  }

  int slot = freeSlot >= 0 ? freeSlot
           : expiredTransient >= 0 ? expiredTransient
           : activeTransient;
  if (slot < 0) {
    if (!persistent) return;
    slot = oldestPersistent;
  }

  Entry& e = entries_[slot];
  e.used = true;
  e.persistent = persistent;
  std::memcpy(e.mac, mac, 6);
  e.sinceMs = nowMs;
  e.durationMs = durationMs;
}

}  // namespace lamp
