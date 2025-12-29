#ifndef LAMP_BEHAVIORS_IDLE_H
#define LAMP_BEHAVIORS_IDLE_H

#include "../core/animated_behavior.hpp"
#include "../util/color.hpp"

/**
 * @brief a base layer of the lamp's default color to prevent blackout
 */
namespace lamp {
class IdleBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  void draw() override;

  void control() override;
};
}  // namespace lamp
#endif