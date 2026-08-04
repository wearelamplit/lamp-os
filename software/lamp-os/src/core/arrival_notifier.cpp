#include "core/arrival_notifier.hpp"

#include <cstring>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#endif

#include "components/network/mesh/lamp_roster.hpp"

namespace lamp {

void ArrivalNotifier::onArrival(Callback cb) {
  if (!cb) return;
  if (observerCount_ >= kMaxObservers) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    Serial.println("[arrival] observer registry full");
#endif
    return;
  }
  observers_[observerCount_++] = std::move(cb);
}

int ArrivalNotifier::firedIndex(const uint8_t mac[6]) const {
  for (size_t i = 0; i < kMaxFired; ++i) {
    if (firedUsed_[i] && std::memcmp(fired_[i], mac, 6) == 0) return static_cast<int>(i);
  }
  return -1;
}

size_t ArrivalNotifier::firedCount() const {
  size_t n = 0;
  for (size_t i = 0; i < kMaxFired; ++i) if (firedUsed_[i]) ++n;
  return n;
}

void ArrivalNotifier::fire(const PeerView& v) {
  for (size_t i = 0; i < observerCount_; ++i) observers_[i](v);
}

void ArrivalNotifier::tick(uint32_t nowMs, LampRoster& roster, uint32_t nearWindowMs) {
  if (observerCount_ == 0) return;
  if (everTicked_ && (nowMs - lastTickMs_) < kThrottleMs) return;
  lastTickMs_ = nowMs;
  everTicked_ = true;

  bool present[kMaxFired] = {};

  // Snapshot before firing: an observer is arbitrary user code that may
  // re-enter getNear/getMesh/getAll, which fills a distinct buffer, so this
  // snapshot stays valid across the fire loop.
  const std::vector<NearbyCopy>& near = roster.snapshotNear(nearWindowMs);
  for (const NearbyCopy& e : near) {
    if (!e.hasMac) continue;
    int idx = firedIndex(e.mac);
    if (idx >= 0) {
      present[idx] = true;  // still near: keep the fired mark, no re-fire
      continue;
    }
    if ((nowMs - e.lastSeenNearMs) > kArrivalMaxAgeMs) continue;  // not fresh

    int slot = -1;
    for (size_t i = 0; i < kMaxFired; ++i) {
      if (!firedUsed_[i]) { slot = static_cast<int>(i); break; }
    }
    if (slot < 0) continue;  // near crowd exceeds kMaxFired; skip, no re-fire spam
    std::memcpy(fired_[slot], e.mac, 6);
    firedUsed_[slot] = true;
    present[slot] = true;
    fire(PeerView::make(e.name, e.mac, e.hasMac, e.baseColor, e.shadeColor,
                        e.lastRssi, e.variant));
  }

  for (size_t i = 0; i < kMaxFired; ++i) {
    if (firedUsed_[i] && !present[i]) firedUsed_[i] = false;  // departed the near window: re-arm
  }
}

}  // namespace lamp
