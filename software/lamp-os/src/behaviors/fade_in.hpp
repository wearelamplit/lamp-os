#ifndef LAMP_BEHAVIORS_FADE_IN_H
#define LAMP_BEHAVIORS_FADE_IN_H

#include "../core/animated_behavior.hpp"

/**
 * @brief animation to fade from black to the lamp default color
 */
namespace lamp {
class FadeInBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  void draw() override;

  void control() override;
};
}  // namespace lamp
#endif