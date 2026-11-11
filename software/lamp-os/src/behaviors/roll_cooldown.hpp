#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lamp {

// One chance roll per source peer per window, in fixed RAM whatever the crowd.
// A roll counts whether or not it fired. A peer whose last roll is younger than
// the window cannot roll; while every slot holds such a roll no new peer can
// either, so a crowd gets at most kSlots rolls per window. The owner calls
// retire() at least once per lampos::kRetiredAgeMs, so a stamp never reads as
// recent one millis() wrap later.
class RollCooldownTable {
 public:
  static constexpr size_t kSlots = 16;

  // True, and stamps `mac` at nowMs, when it may roll now.
  bool tryRoll(const uint8_t mac[6], uint32_t nowMs, uint32_t cooldownMs);
  // Frees every slot whose roll is at least cooldownMs old.
  void retire(uint32_t nowMs, uint32_t cooldownMs);

 private:
  struct Slot {
    uint8_t mac[6] = {};
    bool used = false;
    uint32_t atMs = 0;
  };
  std::array<Slot, kSlots> slots_{};
};

}  // namespace lamp
