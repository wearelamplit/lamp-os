#pragma once

#include <cstdint>
#include <functional>

namespace lamp {

enum class ButtonEvent : uint8_t {
  Click,
  DoubleClick,
  LongPressStart,
  LongPressStop,
};

struct ButtonTiming {
  uint16_t debounceMs = 25;
  uint16_t doubleGapMs = 300;   // second press within this of a release = double
  uint16_t longPressMs = 600;   // held beyond this = long press
};

// Edge-detecting momentary-button FSM. Pure logic: update(pressed, nowMs)
// is fed a debounced-or-raw sample and the injected clock, so it runs
// off-target in native tests. `pressed` is the logical state (true = down),
// leaving active-high/low resolution to the hardware driver.
class Button {
 public:
  explicit Button(ButtonTiming t = {}) : t_(t) {}

  void setCallback(std::function<void(ButtonEvent)> cb) { cb_ = std::move(cb); }

  void update(bool pressed, uint32_t nowMs);

  bool isHeld() const { return debounced_; }
  bool longActive() const { return longActive_; }
  uint32_t heldMs(uint32_t nowMs) const {
    return debounced_ ? nowMs - pressStartMs_ : 0;
  }

 private:
  void emit(ButtonEvent e) { if (cb_) cb_(e); }

  ButtonTiming t_;
  std::function<void(ButtonEvent)> cb_;

  bool rawLast_ = false;
  bool debounced_ = false;
  bool longActive_ = false;
  uint8_t clickCount_ = 0;
  uint32_t lastEdgeMs_ = 0;
  uint32_t pressStartMs_ = 0;
  uint32_t lastReleaseMs_ = 0;
};

}  // namespace lamp
