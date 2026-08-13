
#include "idle.hpp"

namespace lamp {
void IdleBehavior::draw() {
  fb->buffer = fb->defaultColors;

  nextFrame();
};

void IdleBehavior::control() {};
}  // namespace lamp