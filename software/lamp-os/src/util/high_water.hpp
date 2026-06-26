#pragma once

#include <cstdint>

namespace lamp {

// Monotonic high-water mark. observe() folds a sample in and returns the
// running max; the value only ever rises until reset().
//
// Used by both OTA progress indicators so the bar never visually regresses
// when the underlying position moves backward: the distributor's send cursor
// rewinds to serve a smart-REQ, and the receiver's per-session chunk counter
// resets to 0 when a stalled session aborts + re-OFFERs the same image.
class HighWaterMark {
 public:
  uint32_t observe(uint32_t sample) {
    if (sample > value_) value_ = sample;
    return value_;
  }
  uint32_t peek() const { return value_; }
  void reset() { value_ = 0; }

 private:
  uint32_t value_ = 0;
};

}  // namespace lamp
