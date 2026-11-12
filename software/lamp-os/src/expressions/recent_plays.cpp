#include "expressions/recent_plays.hpp"

#include <lampos/clock.hpp>

namespace lamp {

void RecentPlays::record(const ExpressionDescriptor* type, uint32_t nowMs) {
  if (!type) return;
  Stamp* slot = &stamps_[0];
  for (Stamp& s : stamps_) {
    if (s.type == type) {
      slot = &s;
      break;
    }
    if (!s.type) {
      if (slot->type) slot = &s;
    } else if (slot->type && lampos::isLater(slot->atMs, s.atMs)) {
      slot = &s;
    }
  }
  slot->type = type;
  slot->atMs = nowMs;
}

bool RecentPlays::playedWithin(const ExpressionDescriptor* type, uint32_t windowMs, uint32_t nowMs) const {
  if (!type) return false;
  for (const Stamp& s : stamps_) {
    if (s.type == type) return lampos::msSince(nowMs, s.atMs) < windowMs;
  }
  return false;
}

void RecentPlays::retire(uint32_t nowMs) {
  for (Stamp& s : stamps_) {
    if (s.type) s.atMs = lampos::retireStamp(nowMs, s.atMs);
  }
}

}  // namespace lamp
