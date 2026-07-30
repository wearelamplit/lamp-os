#include "components/input/touch.hpp"

#include <algorithm>

namespace lamp {

void Touch::update(uint16_t raw, uint32_t nowMs) {
  if (!calibrated_) {
    if (sampleCount_ < t_.calibrationSamples && sampleCount_ < kMaxCalSamples) {
      samples_[sampleCount_++] = raw;
    }
    if (sampleCount_ >= t_.calibrationSamples || sampleCount_ >= kMaxCalSamples) {
      uint16_t sorted[kMaxCalSamples];
      std::copy(samples_, samples_ + sampleCount_, sorted);
      std::sort(sorted, sorted + sampleCount_);
      baseline_ = sorted[sampleCount_ / 2];
      calibrated_ = true;
    }
    return;
  }

  const int32_t delta =
      static_cast<int32_t>(baseline_) - static_cast<int32_t>(raw);
  if (!touched_) {
    if (delta > t_.pressDelta) {
      touched_ = true;
      touchStartMs_ = nowMs;
      holdFired_ = false;
    } else {
      baseline_ = static_cast<uint16_t>(
          static_cast<int32_t>(baseline_) +
          ((static_cast<int32_t>(raw) - static_cast<int32_t>(baseline_)) >>
           t_.driftShift));
    }
  } else {
    if (delta < t_.releaseDelta) {
      touched_ = false;
      emit(holdFired_ ? TouchEvent::Release : TouchEvent::Tap);
    } else if (!holdFired_ &&
               static_cast<uint32_t>(nowMs - touchStartMs_) >= t_.holdMs) {
      holdFired_ = true;
      emit(TouchEvent::HoldStart);
    }
  }
}

}  // namespace lamp
