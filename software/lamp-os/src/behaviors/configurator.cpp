#include "./configurator.hpp"

#include <cstdint>

#include "../util/color.hpp"
#include "../util/fade.hpp"

namespace lamp {
void ConfiguratorBehavior::draw() {
  for (int i = 0; i < fb->pixelCount; i++) {
    if (frame < easeFrames) {
      fb->buffer[i] = fade(fb->buffer[i], colors[i], easeFrames - 1, frame);
    } else if (frame > (frames - easeFrames)) {
      fb->buffer[i] = fade(colors[i], fb->buffer[i], easeFrames - 1, frame % easeFrames);
    } else {
      fb->buffer[i] = colors[i];
    }
  }

  nextFrame();
};

void ConfiguratorBehavior::control() {
  // If disabled, fade out if needed then stop
  if (disabled) {
    if (animationState == PAUSED) {
      // If we're paused (showing preview), start fading out
      playOnce();
    } else if (animationState == PLAYING_ONCE && frame >= frames) {
      // Fade out complete, now stop
      stop();
    }
    // Don't process normal timeout logic when disabled
    return;
  }

  uint32_t now = millis();
  if (animationState == STOPPED) {
    if (lastWebSocketUpdateTimeMs > 0 &&
        now < lastWebSocketUpdateTimeMs + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS) {
      playOnce();
    }
  }
  if (animationState == PLAYING_ONCE && frame == easeFrames) {
    pause();
  }
  if (animationState == PAUSED && now > lastWebSocketUpdateTimeMs + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS) {
    playOnce();
  }
};
}  // namespace lamp