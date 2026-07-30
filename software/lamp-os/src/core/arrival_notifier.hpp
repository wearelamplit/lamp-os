#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "core/behavior_context.hpp"  // PeerView

namespace lamp {

class LampRoster;

// Push-notifies registered observers once per genuinely-new near peer.
//
// Dedup is framework-owned and does NOT touch the greeting `acknowledged`
// flag: a fixed MAC set records who has fired. A peer re-fires only after it
// leaves the near window (departure, an edge on lastSeenNearMs vs the near
// window) and later returns; roster prune/lifetime is irrelevant, a
// mesh-reachable peer that walks out of BLE range never prunes. The diff is
// millis-gated, not per-frame, so the roster snapshot stays off the hot path
// (docs/dev/embedded-heap.md). Per-peer state is raw MAC bytes; the callbacks
// are a small attach-once set, not per-peer.
class ArrivalNotifier {
 public:
  using Callback = std::function<void(const PeerView&)>;

  static constexpr size_t kMaxObservers = 4;
  static constexpr size_t kMaxFired = 16;
  static constexpr uint32_t kThrottleMs = 750;
  // A sighting older than this is "been around", not a fresh arrival.
  static constexpr uint32_t kArrivalMaxAgeMs = 5000;

  // Attach-once at boot. Bounded; an excess registration is dropped.
  void onArrival(Callback cb);

  // Throttled near-set diff. `nearWindowMs` is the departure hysteresis: a
  // fired peer re-arms once its last near sighting falls outside it, distinct
  // from the short kArrivalMaxAgeMs that bounds a fresh arrival. No-op when no
  // observer is registered or inside the throttle window.
  void tick(uint32_t nowMs, LampRoster& roster, uint32_t nearWindowMs);

  size_t observerCount() const { return observerCount_; }
  size_t firedCount() const;

 private:
  int firedIndex(const uint8_t mac[6]) const;
  void fire(const PeerView& v);

  Callback observers_[kMaxObservers];
  size_t observerCount_ = 0;
  uint8_t fired_[kMaxFired][6] = {};
  bool firedUsed_[kMaxFired] = {};
  uint32_t lastTickMs_ = 0;
  bool everTicked_ = false;
};

}  // namespace lamp

// Single global instance, defined in core/lamp.cpp (mirrors
// expressionObserverRegistry).
extern lamp::ArrivalNotifier arrivalNotifier;
