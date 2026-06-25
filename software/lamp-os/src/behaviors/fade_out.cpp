#include "fade_out.hpp"

#include <Arduino.h>

#include "util/color.hpp"
#include "util/fade.hpp"

namespace lamp {

std::atomic<bool> fadeOutRebootRequested{false};

void FadeOutBehavior::draw() {
  // See FadeInBehavior::draw — same divide-by-zero defence.
  const uint32_t duration = frames > 1 ? frames - 1 : 1;
  for (int i = 0; i < fb->pixelCount; i++) {
    fb->buffer[i] = fade(fb->buffer[i], Color(0, 0, 0, 0), duration, frame);
  }
  nextFrame();
};

void FadeOutBehavior::control() {
  reboot = fadeOutRebootRequested;
  if (animationState == STOPPED && reboot) {
    playOnce();
  }
  if (reboot && isLastFrame()) {
    ESP.restart();
  }
};
}  // namespace lamp
