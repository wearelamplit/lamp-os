#include "compositor.hpp"

#include <Arduino.h>

#include "components/firmware/firmware_distributor.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "components/firmware/ota_indicator.hpp"
#include "components/firmware/ota_quiet_mode.hpp"

extern lamp::FirmwareReceiver firmwareReceiver;

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
#ifdef LAMP_DEBUG
  // Flicker diagnostic: count branch invocations + correlate with OTA
  // state. If non-quiet branch fires while either OTA state machine is
  // in progress, that is the bug — IdleBehavior is the only underlay
  // that draws when nothing else is animating, so the strip strobes
  // between the indicator and full defaultColors.
  static uint32_t s_quietCount    = 0;
  static uint32_t s_normalCount   = 0;
  static uint32_t s_otaQuietHits  = 0;   // non-quiet branch while OTA in progress
  static uint32_t s_lastLogMs     = 0;
  const bool otaActive = ::firmwareReceiver.isInProgress() ||
                         lamp::firmwareDistributor.isInProgress();
#endif
  // OTA quiet mode: skip the normal behavior pipeline, but paint the OTA
  // progress indicator (see ota_indicator.hpp) and flush so the user sees
  // a live "I'm in OTA right now" signal instead of a frozen frame.
  // Indicator is cheap (fill + per-pixel scale), well within frame budget.
  if (ota_quiet_mode::isQuiet()) {
    const uint32_t nowMs = millis();
#ifdef LAMP_DEBUG
    s_quietCount++;
#endif
    // Cap the indicator repaint to the normal pipeline's ~60 Hz frame rate
    // (MINIMUM_FRAME_DRAW_TIME_MS). FrameBuffer::flush() already content-dedups
    // and gates the strip write on the driver's reset window, so this is a CPU
    // cap. Without it the quiet branch re-runs the
    // paint every loop iteration for no visible gain.
    if (millis() < lastDrawTimeMs + MINIMUM_FRAME_DRAW_TIME_MS) {
      return;
    }
    lastDrawTimeMs = millis();
    const bool quietEntered = !otaQuietPainted_;
    otaQuietPainted_ = true;
    // frameBuffers[0]/[1] per lamp_behaviors.cpp ordering: {shade, base}.
    // Visible (firmware) OTA paints the progress bar on shade and settles +
    // pulses base; silent (FS) OTA settles both surfaces with no signaling.
    if (!frameBuffers.empty() && frameBuffers[0]) {
      FrameBuffer* shade = frameBuffers[0];
      FrameBuffer* base = frameBuffers.size() > 1 ? frameBuffers[1] : nullptr;
      if (ota_quiet_mode::visibleOtaActive()) {
        const Color localBase = shade->defaultColors.empty()
                                    ? Color(0, 0, 255, 0)
                                    : shade->defaultColors.front();
        ota_indicator::paint(shade, localBase, nowMs, base, quietEntered);
      } else {
        ota_indicator::paintSilent(shade, base, nowMs, quietEntered);
      }
    }
    if (preFlushHook) preFlushHook();
    for (size_t i = 0; i < frameBuffers.size(); i++) {
      if (frameBuffers[i]) frameBuffers[i]->flush();
    }
#ifdef LAMP_DEBUG
    if (millis() - s_lastLogMs >= 1000) {
      s_lastLogMs = millis();
      Serial.printf("[compos] quiet=%u normal=%u otaQuietHits=%u (ota=%d)\n",
                    (unsigned)s_quietCount, (unsigned)s_normalCount,
                    (unsigned)s_otaQuietHits, (int)otaActive);
      s_quietCount   = 0;
      s_normalCount  = 0;
      s_otaQuietHits = 0;
    }
#endif
    return;
  }
  otaQuietPainted_ = false;

#ifdef LAMP_DEBUG
  // Non-quiet branch. If OTA is active here we have the bug — the
  // pipeline is going to underlay (IdleBehavior) → defaultColors and
  // overwrite the indicator's just-flushed bar.
  s_normalCount++;
  if (otaActive) s_otaQuietHits++;
  if (millis() - s_lastLogMs >= 1000) {
    s_lastLogMs = millis();
    Serial.printf("[compos] quiet=%u normal=%u otaQuietHits=%u (ota=%d)\n",
                  (unsigned)s_quietCount, (unsigned)s_normalCount,
                  (unsigned)s_otaQuietHits, (int)otaActive);
    s_quietCount   = 0;
    s_normalCount  = 0;
    s_otaQuietHits = 0;
  }
#endif

  if (!behaviorsComputed) {
    for (size_t i = 0; i < underlayBehaviors.size(); i++) {
      underlayBehaviors[i]->control();
      underlayBehaviors[i]->draw();
    }

    if (startupComplete) {
      for (size_t i = 0; i < behaviors.size(); i++) {
        if (!homeMode || !homeModeSkips(behaviors[i])) {
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
    if (preFlushHook) preFlushHook();
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

void Compositor::setExpressionBand(size_t start, size_t end) {
  expressionBandStart = start;
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

void Compositor::addBaseBehavior(AnimatedBehavior* b) {
  if (!b) return;
  b->setBehaviorContext(&context_);
  if (expressionBandStart > behaviors.size()) expressionBandStart = behaviors.size();
  behaviors.insert(behaviors.begin() + expressionBandStart, b);
  expressionBandStart++;
  expressionBandEnd++;
}

void Compositor::removeBehavior(AnimatedBehavior* b) {
  if (!b) return;
  for (size_t idx = 0; idx < behaviors.size(); idx++) {
    if (behaviors[idx] == b) {
      behaviors.erase(behaviors.begin() + idx);
      if (idx < expressionBandStart) expressionBandStart--;
      if (idx < expressionBandEnd) expressionBandEnd--;
      return;
    }
  }
}

void Compositor::setHomeModePolicy(bool socialDisabled, std::vector<std::string> disabledIds) {
  if (homeSocialDisabled_ != socialDisabled || homeDisabledExprIds_ != disabledIds) {
    homeSocialDisabled_ = socialDisabled;
    homeDisabledExprIds_ = std::move(disabledIds);
    behaviorsComputed = false;
  }
}

bool Compositor::homeModeSkips(AnimatedBehavior* b) const {
  if (b->isSocialBehavior()) return homeSocialDisabled_;
  return homeModeExpressionSkips(b->homeModeExpressionId(), homeDisabledExprIds_);
}

bool Compositor::homeModeSkipsType(const std::string& type) const {
  return homeMode && homeModeExpressionSkips(type.c_str(), homeDisabledExprIds_);
}

};  // namespace lamp
