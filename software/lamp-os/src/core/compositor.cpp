#include "compositor.hpp"

namespace lamp {
Compositor::Compositor() {
  // Self-publish so behaviors registered later can reach the compositor
  // without a global. ExpressionManager / FrameBuffer fields are wired by
  // their respective owners (lamp.cpp).
  context_.compositor = this;
};

void Compositor::begin(std::vector<AnimatedBehavior*> inBehaviors,
                       std::vector<FrameBuffer*> inFrameBuffers,
                       std::vector<AnimatedBehavior*> inUnderlayBehaviors,
                       std::vector<AnimatedBehavior*> inStartupBehaviors,
                       bool homeMode) {
  frameBuffers = inFrameBuffers;
  this->homeMode = homeMode;

  // Caller-provided underlay + startup behaviors. Construction of concrete
  // types (IdleBehavior, FadeInBehavior) is the caller's responsibility;
  // the compositor only wires context and stores the pointers.
  for (auto* b : inUnderlayBehaviors) {
    b->setBehaviorContext(&context_);
    underlayBehaviors.push_back(b);
  }
  for (auto* b : inStartupBehaviors) {
    b->setBehaviorContext(&context_);
    startupBehaviors.push_back(b);
  }

  // append all of the non critical behaviors
  for (size_t i = 0; i < inBehaviors.size(); i++) {
    inBehaviors[i]->setBehaviorContext(&context_);
    behaviors.push_back(inBehaviors[i]);
  }
};

void Compositor::tick() {
  if (!behaviorsComputed) {
    for (size_t i = 0; i < underlayBehaviors.size(); i++) {
      underlayBehaviors[i]->control();
      underlayBehaviors[i]->draw();
    }

    if (startupComplete) {
      for (size_t i = 0; i < behaviors.size(); i++) {
        if (!homeMode || behaviors[i]->allowedInHomeMode) {
          behaviors[i]->control();
          if (behaviors[i]->animationState != STOPPED) {
            behaviors[i]->draw();
          }
        }
      }
    } else {
      for (size_t i = 0; i < startupBehaviors.size(); i++) {
        startupBehaviors[i]->control();
        if (startupBehaviors[i]->animationState != STOPPED) {
          startupBehaviors[i]->draw();
        }
        if (millis() > 3000) {
          startupComplete = true;
        }
      }
    }

    for (size_t i = 0; i < overlayBehaviors.size(); i++) {
      overlayBehaviors[i]->control();
      overlayBehaviors[i]->draw();
    }

    behaviorsComputed = true;
  }

  if (behaviorsComputed && millis() >= lastDrawTimeMs + MINIMUM_FRAME_DRAW_TIME_MS) {
    lastDrawTimeMs = millis();
    behaviorsComputed = false;
    for (size_t i = 0; i < frameBuffers.size(); i++) {
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

void Compositor::setExpressionBandEnd(size_t end) {
  expressionBandEnd = end;
}

void Compositor::addBehavior(AnimatedBehavior* b) {
  if (!b) return;
  // Wire the shared context on register so behaviors don't need to grab a
  // global to reach the compositor / expression manager / buffer list.
  b->setBehaviorContext(&context_);
  if (expressionBandEnd > behaviors.size()) expressionBandEnd = behaviors.size();
  behaviors.insert(behaviors.begin() + expressionBandEnd, b);
  expressionBandEnd++;
}

void Compositor::removeBehavior(AnimatedBehavior* b) {
  if (!b) return;
  for (size_t idx = 0; idx < behaviors.size(); idx++) {
    if (behaviors[idx] == b) {
      behaviors.erase(behaviors.begin() + idx);
      if (idx < expressionBandEnd) expressionBandEnd--;
      return;
    }
  }
}
};  // namespace lamp
