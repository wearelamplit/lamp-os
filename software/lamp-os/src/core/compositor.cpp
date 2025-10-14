#include "./compositor.hpp"

#include "./behaviors/fade_in.hpp"
#include "./behaviors/idle.hpp"

namespace lamp {
Compositor::Compositor() {};

void Compositor::begin(std::vector<AnimatedBehavior*> inBehaviors, std::vector<FrameBuffer*> inFrameBuffers, bool homeMode) {
  frameBuffers = inFrameBuffers;
  this->homeMode = homeMode;

  // Adds some basic behavior layers that are common to all framebuffers
  for (i = 0; i < frameBuffers.size(); i++) {
    behaviors.push_back(new IdleBehavior(frameBuffers[i], 0, true));
    startupBehaviors.push_back(new IdleBehavior(frameBuffers[i], 0, true));
    startupBehaviors.push_back(new FadeInBehavior(frameBuffers[i], STARTUP_ANIMATION_FRAMES));
  }

  // append all of the non critical behaviors
  for (i = 0; i < inBehaviors.size(); i++) {
    behaviors.push_back(inBehaviors[i]);
  }
};

bool Compositor::hasActiveExclusive() const {
  return activeExclusive && activeExclusive->animationState != STOPPED;
}

void Compositor::tick() {
  if (!behaviorsComputed) {
    if (startupComplete) {
      // Update active exclusive tracker
      if (activeExclusive && activeExclusive->animationState == STOPPED) {
        activeExclusive = nullptr;
      }

      for (i = 0; i < behaviors.size(); i++) {
        if (!homeMode || behaviors[i]->allowedInHomeMode) {
          // Check if this behavior should run
          bool canRun = !activeExclusive || behaviors[i]->isExclusive || behaviors[i] == activeExclusive;

          if (canRun) {
            behaviors[i]->control();
            if (behaviors[i]->animationState != STOPPED) {
              // Track if this is a new exclusive starting
              if (behaviors[i]->isExclusive && behaviors[i] != activeExclusive) {
                activeExclusive = behaviors[i];
              }
              behaviors[i]->draw();
            }
          }
        }
      }
    } else {
      for (i = 0; i < startupBehaviors.size(); i++) {
        startupBehaviors[i]->control();
        if (startupBehaviors[i]->animationState != STOPPED) {
          startupBehaviors[i]->draw();
        }
        if (millis() > 3000) {
          startupComplete = true;
        }
      }
    }

    for (i = 0; i < overlayBehaviors.size(); i++) {
      overlayBehaviors[i]->control();
      overlayBehaviors[i]->draw();
    }

    behaviorsComputed = true;
  }

  if (behaviorsComputed && millis() >= lastDrawTimeMs + MINIMUM_FRAME_DRAW_TIME_MS) {
    lastDrawTimeMs = millis();
    behaviorsComputed = false;
    for (i = 0; i < frameBuffers.size(); i++) {
      frameBuffers[i]->flush();
    }
  };
};

void Compositor::setHomeMode(bool homeMode) {
  if (this->homeMode != homeMode) {
    this->homeMode = homeMode;
    behaviorsComputed = false;  // Force recomputation of active behaviors
  }
};
};  // namespace lamp
