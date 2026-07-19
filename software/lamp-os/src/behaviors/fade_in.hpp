#pragma once

#include "core/animated_behavior.hpp"

/**
 * animation to fade from black to the lamp default color
 */
namespace lamp {
class FadeInBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  void draw() override;

  void control() override;
};
}  // namespace lamp
