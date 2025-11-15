#include "./dmx.hpp"

#include <cstdint>

#include "../util/color.hpp"
#include "../util/fade.hpp"

namespace lamp {
void DmxBehavior::draw() {
  for (i = 0; i < fb->pixelCount; i++) {
    if (frame < easeFrames) {
      fb->buffer[i] = fade(fb->buffer[i], currentColor, easeFrames - 1, frame);
    } else if (frame > (frames - easeFrames)) {
      fb->buffer[i] = fade(currentColor, fb->buffer[i], easeFrames - 1, frame % easeFrames);
    } else {
      fb->buffer[i] = currentColor;
    }
  }

  nextFrame();
};

void DmxBehavior::control() {
  uint32_t now = millis();

  if (animationState == STOPPED) {
    if (lastArtnetFrameTimeMs > 0 && now < lastArtnetFrameTimeMs + DMX_ARTNET_TIMEOUT_MS) {
      currentColor = currentDmxColor;  // sample and hold a value from dmx for transition in
      transitionFrame = 0;
      playOnce();
    }
  }

  if (animationState == PLAYING_ONCE && frame == easeFrames * 2) {
    pause();
  }

  if (animationState == PAUSED && now > lastArtnetFrameTimeMs + DMX_ARTNET_TIMEOUT_MS) {
    transitionFrame = 0;
    playOnce();
  }

  if (animationState == PAUSED) {
    // the lamp will now begin a sub animation to smear between what dmx is sending
    if (transitionFrame == 0) {
      // create the ranges
      startColor = currentColor;
      endColor = currentDmxColor;
      transitionFrames = random(DMX_BEHAVIOR_FADE_TIME_MIN_FRAMES, DMX_BEHAVIOR_FADE_TIME_MAX_FRAMES);

      // if the random time is long and there's not enough steps, speed up the transition this round
      if (transitionFrames > 300 && colorDistance(startColor, endColor) < 550) {
        transitionFrames = random(DMX_BEHAVIOR_FADE_TIME_MIN_FRAMES, DMX_BEHAVIOR_FADE_TIME_LOW_STEPS_FRAMES);
      }
    }

    if (transitionFrames > DMX_BEHAVIOR_FADE_TIME_LOW_STEPS_FRAMES) {
      // use a smoother algo for longer transitions to prevent obvious led changes
      currentColor = fadeLinear(startColor, endColor, transitionFrames, transitionFrame);
    } else {
      // use a more exciting algo for short transitions
      currentColor = fade(startColor, endColor, transitionFrames, transitionFrame);
    }

    transitionFrame++;

    if (transitionFrame >= transitionFrames) {
      transitionFrame = 0;
    }
  }
};

void DmxBehavior::setColor(Color inColor) {
  currentDmxColor = inColor;
};

void DmxBehavior::setLastArtnetFrameTimeMs(uint32_t inLastArtnetFrameTimeMs) {
  lastArtnetFrameTimeMs = inLastArtnetFrameTimeMs;
};
}  // namespace lamp