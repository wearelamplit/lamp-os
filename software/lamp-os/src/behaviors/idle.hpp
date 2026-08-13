#pragma once

#include "core/animated_behavior.hpp"
#include "util/color.hpp"

/**
 * a base layer of the lamp's default color to prevent blackout
 */
namespace lamp {
class IdleBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  void draw() override;

  void control() override;
};
}  // namespace lamp
