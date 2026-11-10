#include "net/peer_slots.hpp"

#include <cstring>

#include "util/uptime.hpp"

namespace wisp {

PeerSlots::Change PeerSlots::touch(const uint8_t mac[6]) {
  const uint64_t nowMs = uptimeMs();
  Change change;
  Slot* target = nullptr;
  for (Slot& s : slots_) {
    if (s.used && std::memcmp(s.mac, mac, 6) == 0) {
      s.lastUsedMs = nowMs;
      return change;
    }
    if (!s.used && !target) target = &s;
  }
  if (!target) {
    target = &slots_[0];
    for (Slot& s : slots_) {
      if (s.lastUsedMs < target->lastUsedMs) target = &s;
    }
    change.evicted = true;
    std::memcpy(change.evictedMac, target->mac, 6);
  }
  std::memcpy(target->mac, mac, 6);
  target->lastUsedMs = nowMs;
  target->used = true;
  change.added = true;
  return change;
}

void PeerSlots::release(const uint8_t mac[6]) {
  for (Slot& s : slots_) {
    if (s.used && std::memcmp(s.mac, mac, 6) == 0) s.used = false;
  }
}

}  // namespace wisp
