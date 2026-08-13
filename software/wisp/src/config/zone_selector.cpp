#include "config/zone_selector.hpp"

#include <cstring>

namespace wisp {

const char* zoneSourceName(ZoneSource s) {
  switch (s) {
    case ZoneSource::None:      return "none";
    case ZoneSource::FirstSeen: return "firstSeen";
    case ZoneSource::Nvs:       return "nvs";
    case ZoneSource::AppOp:     return "appOp";
  }
  return "?";
}

void ZoneSelector::observe(int zone) {
  if (!isValidZone(zone)) return;
  WISP_ZONE_PORTMUX_ENTER(&observedMux_);
  bool present = false;
  for (size_t i = 0; i < observedCount_; ++i)
    if (observedZones_[i] == zone) { present = true; break; }
  if (!present) {
    if (observedCount_ >= kMaxObservedZones) {       // oldest-out FIFO
      memmove(observedZones_, observedZones_ + 1,
              (kMaxObservedZones - 1) * sizeof(int));
      observedCount_ = kMaxObservedZones - 1;
    }
    observedZones_[observedCount_++] = zone;
  }
  WISP_ZONE_PORTMUX_EXIT(&observedMux_);
}

size_t ZoneSelector::copyObserved(int* out, size_t outCap) const {
  if (!out || outCap == 0) return 0;
  size_t n = 0;
  WISP_ZONE_PORTMUX_ENTER(&observedMux_);
  n = observedCount_ < outCap ? observedCount_ : outCap;
  for (size_t i = 0; i < n; ++i) out[i] = observedZones_[i];
  WISP_ZONE_PORTMUX_EXIT(&observedMux_);
  return n;
}

bool ZoneSelector::latchFirstSeen(int zone) {
  if (!isValidZone(zone)) return false;
  if (source_ != ZoneSource::None || currentZone_ >= 0) return false;
  currentZone_ = zone;
  source_ = ZoneSource::FirstSeen;
  return true;
}

void ZoneSelector::setFromOp(int zone) {
  if (!isValidZone(zone)) return;
  currentZone_ = zone;
  source_ = ZoneSource::AppOp;
}

void ZoneSelector::clearFromOp() {
  currentZone_ = -1;
  source_ = ZoneSource::None;
}

void ZoneSelector::setFromNvs(int zone) {
  if (!isValidZone(zone)) return;
  currentZone_ = zone;
  source_ = ZoneSource::Nvs;
}

}  // namespace wisp
