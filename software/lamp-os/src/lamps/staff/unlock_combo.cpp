#include "lamps/staff/unlock_combo.hpp"

namespace staff {

void UnlockCombo::stokeLongReleased(uint32_t nowMs) {
  if (state_ == State::Unlocked) return;
  state_ = State::Armed;
  armedMs_ = nowMs;
}

void UnlockCombo::topPadTapped(uint32_t nowMs) {
  if (state_ != State::Armed) return;
  if (static_cast<uint32_t>(nowMs - armedMs_) <= t_.stepWindowMs) {
    state_ = State::Unlocked;
  } else {
    state_ = State::Locked;
  }
}

void UnlockCombo::tick(uint32_t nowMs) {
  if (state_ == State::Armed &&
      static_cast<uint32_t>(nowMs - armedMs_) > t_.stepWindowMs) {
    state_ = State::Locked;
  }
}

}  // namespace staff
