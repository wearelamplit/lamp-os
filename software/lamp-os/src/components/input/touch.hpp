#pragma once

#include <cstdint>
#include <functional>

namespace lamp {

enum class TouchEvent : uint8_t {
  Tap,        // press + release shorter than holdMs
  HoldStart,  // held past holdMs
  Release,    // released after a HoldStart
};

struct TouchTuning {
  uint8_t  calibrationSamples = 16;  // median of N reads seeds the baseline
  uint16_t pressDelta = 12;          // baseline - raw above this = touched
  uint16_t releaseDelta = 6;         // baseline - raw below this = released (hysteresis)
  uint16_t holdMs = 400;             // touched past this = hold, else tap on release
  uint8_t  driftShift = 6;           // EMA shift tracking baseline drift when untouched
};

// Capacitive-pad gesture FSM. touched = raw reads LOWER than the baseline.
// Pure logic: update(raw, nowMs) is fed a raw touchRead sample and the
// injected clock so it runs off-target in native tests. The baseline is
// the median of the first `calibrationSamples` fed reads; the driver
// gates WHEN feeding starts (after RF is up) so calibration captures a
// radio-live ambient, not a pre-radio-quiet value.
class Touch {
 public:
  static constexpr uint8_t kMaxCalSamples = 32;

  explicit Touch(TouchTuning t = {}) : t_(t) {}

  void setCallback(std::function<void(TouchEvent)> cb) { cb_ = std::move(cb); }

  void update(uint16_t raw, uint32_t nowMs);

  bool isCalibrated() const { return calibrated_; }
  uint16_t baseline() const { return baseline_; }
  bool isHeld() const { return touched_ && holdFired_; }
  uint32_t heldMs(uint32_t nowMs) const {
    return touched_ ? nowMs - touchStartMs_ : 0;
  }

 private:
  void emit(TouchEvent e) { if (cb_) cb_(e); }

  TouchTuning t_;
  std::function<void(TouchEvent)> cb_;

  uint16_t samples_[kMaxCalSamples] = {0};
  uint8_t sampleCount_ = 0;
  bool calibrated_ = false;
  uint16_t baseline_ = 0;

  bool touched_ = false;
  bool holdFired_ = false;
  uint32_t touchStartMs_ = 0;
};

}  // namespace lamp
