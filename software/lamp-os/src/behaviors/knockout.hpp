#pragma once

#include "core/animated_behavior.hpp"

/**
 * selectively darken parts of the lamp to handle brightness of the
 *        leds in the lamp base
 * @property knockoutPixels - a level per base pixel; pixels past its end are
 *           left at full level
 */
namespace lamp {
class KnockoutBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  std::vector<uint8_t> knockoutPixels = std::vector<uint8_t>(50, (uint8_t)100);

  void draw() override;
  void control() override;
};
}  // namespace lamp
