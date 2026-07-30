#include "components/input/button.hpp"

namespace lamp {

void Button::update(bool pressed, uint32_t nowMs) {
  if (pressed != rawLast_) {
    rawLast_ = pressed;
    lastEdgeMs_ = nowMs;
  }
  if (pressed != debounced_ &&
      static_cast<uint32_t>(nowMs - lastEdgeMs_) >= t_.debounceMs) {
    debounced_ = pressed;
    if (pressed) {
      pressStartMs_ = nowMs;
      longActive_ = false;
    } else if (longActive_) {
      longActive_ = false;
      clickCount_ = 0;
      emit(ButtonEvent::LongPressStop);
    } else {
      ++clickCount_;
      lastReleaseMs_ = nowMs;
    }
  }

  if (debounced_ && !longActive_ &&
      static_cast<uint32_t>(nowMs - pressStartMs_) >= t_.longPressMs) {
    longActive_ = true;
    clickCount_ = 0;
    emit(ButtonEvent::LongPressStart);
  }

  if (!debounced_ && clickCount_ > 0 &&
      static_cast<uint32_t>(nowMs - lastReleaseMs_) >= t_.doubleGapMs) {
    emit(clickCount_ >= 2 ? ButtonEvent::DoubleClick : ButtonEvent::Click);
    clickCount_ = 0;
  }
}

}  // namespace lamp
