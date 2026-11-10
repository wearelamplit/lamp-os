#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <lampos/clock.hpp>

namespace lamp {

// Cascade dedup window. The TARGET_BOTH auto-trigger from Expression::control()
// fires shade+base entries back-to-back in the same loop tick (microseconds
// apart), so anything wider than a frame would catch the double-fire; 250 ms
// also tolerates a generous control()-iteration slip without being so loose
// it suppresses a deliberate back-to-back manual trigger.
static constexpr uint32_t kCascadeDedupWindowMs = 250;

// Small ring of (type, intervalIdx, fireMs) for fires already sent on, so the
// second entry of one logical trigger is suppressed. CAPACITY=8 is generous:
// a logical trigger records at most two entries (TARGET_BOTH).
class RecentCascade {
 public:
  static constexpr size_t CAPACITY = 8;

  // Whether (type, intervalIdx) was recorded within kCascadeDedupWindowMs.
  bool seen(const std::string& type, uint32_t intervalIdx, uint32_t nowMs) const {
    for (const Entry& e : entries_) {
      if (!e.used) continue;
      if (e.type != type || e.intervalIdx != intervalIdx) continue;
      if (nowMs - e.fireMs <= kCascadeDedupWindowMs) return true;
    }
    return false;
  }

  // Overwrites the oldest slot by insertion.
  void record(const std::string& type, uint32_t intervalIdx, uint32_t nowMs) {
    Entry& slot = entries_[head_];
    slot.used = true;
    slot.type = type;
    slot.intervalIdx = intervalIdx;
    slot.fireMs = nowMs;
    head_ = (head_ + 1) % CAPACITY;
  }

  // Drops fires past the window, so none reads as recent again one millis()
  // wrap later. Call at least once per wrap.
  void retire(uint32_t nowMs) {
    for (Entry& e : entries_) {
      if (e.used && lampos::msSince(nowMs, e.fireMs) > kCascadeDedupWindowMs) e.used = false;
    }
  }

 private:
  struct Entry {
    bool used = false;
    std::string type;
    uint32_t intervalIdx = 0;
    uint32_t fireMs = 0;
  };
  std::array<Entry, CAPACITY> entries_{};
  size_t head_ = 0;
};

}  // namespace lamp
