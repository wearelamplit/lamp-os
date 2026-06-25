#include "WispZoneSelector.h"

#include <algorithm>

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
  // The full lookup + erase + push_back lives in the critical section so a
  // concurrent copyObserved() snapshot from the timer-service task can't
  // catch the vector mid-relocation.
  WISP_ZONE_PORTMUX_ENTER(&observedMux_);
  auto it = std::find(observedZones_.begin(), observedZones_.end(), zone);
  if (it == observedZones_.end()) {
    if (observedZones_.size() >= kMaxObservedZones) {
      observedZones_.erase(observedZones_.begin());  // oldest-out FIFO
    }
    observedZones_.push_back(zone);
  }
  WISP_ZONE_PORTMUX_EXIT(&observedMux_);
}

size_t ZoneSelector::copyObserved(int* out, size_t outCap) const {
  if (!out || outCap == 0) return 0;
  size_t n = 0;
  WISP_ZONE_PORTMUX_ENTER(&observedMux_);
  n = observedZones_.size();
  if (n > outCap) n = outCap;
  for (size_t i = 0; i < n; ++i) out[i] = observedZones_[i];
  WISP_ZONE_PORTMUX_EXIT(&observedMux_);
  return n;
}

bool ZoneSelector::latchFirstSeen(int zone) {
  if (source_ != ZoneSource::None || currentZone_ >= 0) return false;
  currentZone_ = zone;
  source_ = ZoneSource::FirstSeen;
  return true;
}

void ZoneSelector::setFromOp(int zone) {
  currentZone_ = zone;
  source_ = ZoneSource::AppOp;
}

void ZoneSelector::clearFromOp() {
  currentZone_ = -1;
  source_ = ZoneSource::None;
}

void ZoneSelector::setFromNvs(int zone) {
  currentZone_ = zone;
  source_ = ZoneSource::Nvs;
}

}  // namespace wisp
