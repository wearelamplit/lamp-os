#pragma once

#include <cstdint>

namespace staff {

struct UnlockTiming {
  uint16_t stepWindowMs = 1500;  // tap the top pad within this of the stoke release
};

// Two-step unlock gate for the staff's touch controls. Locked by default;
// touch does nothing until unlocked. Step 1 is a deliberate stoke long-press
// then release; step 2 is a top-pad tap inside the window. A tap outside the
// window re-locks. Pure logic + injected clock (native-testable). No
// persistence: a reboot re-locks.
class UnlockCombo {
 public:
  explicit UnlockCombo(UnlockTiming t = {}) : t_(t) {}

  bool unlocked() const { return state_ == State::Unlocked; }
  bool armed() const { return state_ == State::Armed; }

  void stokeLongReleased(uint32_t nowMs);
  void topPadTapped(uint32_t nowMs);
  void tick(uint32_t nowMs);

 private:
  enum class State : uint8_t { Locked, Armed, Unlocked };
  State state_ = State::Locked;
  uint32_t armedMs_ = 0;
  UnlockTiming t_;
};

}  // namespace staff
