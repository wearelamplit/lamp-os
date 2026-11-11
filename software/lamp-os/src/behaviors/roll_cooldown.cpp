#include "behaviors/roll_cooldown.hpp"

#include <cstring>

#include <lampos/clock.hpp>

namespace lamp {

bool RollCooldownTable::tryRoll(const uint8_t mac[6], uint32_t nowMs, uint32_t cooldownMs) {
  Slot* open = nullptr;
  for (Slot& s : slots_) {
    const bool live = s.used && lampos::msSince(nowMs, s.atMs) < cooldownMs;
    if (!live) {
      if (!open) open = &s;
    } else if (std::memcmp(s.mac, mac, 6) == 0) {
      return false;
    }
  }
  if (!open) return false;
  std::memcpy(open->mac, mac, 6);
  open->used = true;
  open->atMs = nowMs;
  return true;
}

void RollCooldownTable::retire(uint32_t nowMs, uint32_t cooldownMs) {
  for (Slot& s : slots_) {
    if (s.used && lampos::msSince(nowMs, s.atMs) >= cooldownMs) s.used = false;
  }
}

}  // namespace lamp
