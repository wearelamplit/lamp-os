#include "components/input/input_driver.hpp"

#include <Arduino.h>

#include "core/hw_config.hpp"

namespace lamp {
namespace input {

namespace {

// Momentary button wired active-low against the internal pull-up: LOW = pressed.
class ButtonInput : public InputSource {
 public:
  explicit ButtonInput(const InputSpec& spec)
      : InputSource(spec.id, InputType::Button), pin_(spec.pin) {
    pinMode(pin_, INPUT_PULLUP);
  }

  void tick(uint32_t nowMs) override {
    button_.update(digitalRead(pin_) == LOW, nowMs);
  }

  Button* asButton() override { return &button_; }

 private:
  uint8_t pin_;
  Button button_;
};

// Capacitive pad read via touchRead, polled at ~25 Hz. touchRead is a local
// SAR/RTC peripheral read (sub-ms, no RF path), safe on Core 1. Sampling
// starts after a short warmup so baseline calibration captures a radio-live
// ambient rather than the pre-radio-quiet boot value.
class TouchInput : public InputSource {
 public:
  static constexpr uint16_t kReadIntervalMs = 40;
  static constexpr uint16_t kWarmupMs = 500;

  explicit TouchInput(const InputSpec& spec)
      : InputSource(spec.id, InputType::Touch),
        pin_(spec.pin),
        touch_(TouchTuning{16, spec.touchPressDelta, spec.touchReleaseDelta,
                           spec.touchHoldMs, 6}),
        createdMs_(millis()) {}

  void tick(uint32_t nowMs) override {
    if (static_cast<uint32_t>(nowMs - createdMs_) < kWarmupMs) return;
    if (static_cast<uint32_t>(nowMs - lastReadMs_) < kReadIntervalMs) return;
    lastReadMs_ = nowMs;
    touch_.update(static_cast<uint16_t>(touchRead(pin_)), nowMs);
  }

  Touch* asTouch() override { return &touch_; }

 private:
  uint8_t pin_;
  Touch touch_;
  uint32_t createdMs_;
  uint32_t lastReadMs_ = 0;
};

}  // namespace

std::vector<std::unique_ptr<InputSource>> buildInputs(
    const std::vector<InputSpec>& specs) {
  std::vector<std::unique_ptr<InputSource>> out;
  out.reserve(specs.size());
  for (const InputSpec& spec : specs) {
    switch (spec.type) {
      case InputType::Button:
        out.push_back(std::make_unique<ButtonInput>(spec));
        break;
      case InputType::Touch:
        out.push_back(std::make_unique<TouchInput>(spec));
        break;
    }
  }
  return out;
}

}  // namespace input
}  // namespace lamp
