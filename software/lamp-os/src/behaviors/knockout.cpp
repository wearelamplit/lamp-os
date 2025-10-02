#include "./knockout.hpp"

#include "./util/levels.hpp"

namespace lamp {
void KnockoutBehavior::draw() {
  for (int i = 0; i < fb->pixelCount; i++) {
    fb->buffer[i] = setColorBrightness(fb->buffer[i], knockoutPixels[i]);
  }
};

void KnockoutBehavior::control() {};
}  // namespace lamp
