#include "knockout.hpp"

#include <algorithm>

#include "util/levels.hpp"

namespace lamp {
void KnockoutBehavior::draw() {
  const size_t n = std::min<size_t>(fb->pixelCount, knockoutPixels.size());
  for (size_t i = 0; i < n; i++) {
    fb->buffer[i] = setColorBrightness(fb->buffer[i], knockoutPixels[i]);
  }
};

void KnockoutBehavior::control() {};
}  // namespace lamp
