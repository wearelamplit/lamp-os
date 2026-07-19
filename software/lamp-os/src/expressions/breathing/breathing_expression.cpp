#include "expressions/breathing/breathing_expression.hpp"

#include <Arduino.h>
#include <algorithm>

#include "util/fade.hpp"

namespace lamp {

namespace {
constexpr ExpressionDescriptor kBreathingDescriptor =
    withMake(kBreathingDescriptorData, &makeExpr<BreathingExpression>);
}  // namespace

const ExpressionDescriptor& BreathingExpression::classDescriptor() {
  return kBreathingDescriptor;
}

const ExpressionDescriptor& BreathingExpression::descriptor() const {
  return kBreathingDescriptor;
}

BreathingExpression::BreathingExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
}

void BreathingExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  uint32_t breathSpeed = getParam(parameters, "breathSpeed");
  if (breathSpeed < 8) breathSpeed = 8;
  breathSpeedMs = breathSpeed * kMsPerSecond;

  targetColor = firstColorOr(kSafeFallbackColor);

  const uint16_t window = windowSize();
  zone_ = resolveZone(parameters, window);
  uint32_t s = getParam(parameters, "sections", 1);
  sections_ = static_cast<uint16_t>(std::clamp<uint32_t>(s, 1, 5));
  easing_ = static_cast<Easing>(getParam(parameters, "easing", 1));

  usableSections_ = usableSections(sections_, zone_.size());
  randomPermutation(sectionOrder_, usableSections_, rng);
}

void BreathingExpression::updateBreathPhase() {
  uint32_t currentMs = millis();

  if (lastBreathUpdateMs == 0) {
    lastBreathUpdateMs = currentMs;
    return;
  }

  uint32_t deltaMs = currentMs - lastBreathUpdateMs;

  // Update phase based on breath speed
  // breathSpeedMs is the total cycle time, so increment is deltaMs / breathSpeedMs
  float phaseIncrement = static_cast<float>(deltaMs) / static_cast<float>(breathSpeedMs);
  breathPhase += phaseIncrement;

  // Wrap phase to stay in 0-1 range and advance color when cycle completes
  if (breathPhase >= 1.0f) {
    breathPhase -= 1.0f;

    // Transient one-shot path: when autoTriggerEnabled is false, this
    // expression was created by ExpressionManager::triggerInvocation as a
    // remote-cascaded one-breath instance.
    // BreathingExpression normally runs continuously with frames=100000 and
    // animationState=PLAYING, which never naturally reaches STOPPED, so
    // gcTransients() would never reap it (memory leak). Mark one complete
    // cycle here so isAnimationComplete() returns true on the next tick.
    if (!autoTriggerEnabled) {
      animationState = STOPPED;
      lastCompletedLoop = currentLoop + 1;
      lastBreathUpdateMs = currentMs;
      return;
    }

    if (colors.size() > 1) {
      targetColor = getRandomColor();
    }
  }

  lastBreathUpdateMs = currentMs;
}

void BreathingExpression::onTrigger() {
  breathPhase = 0.0f;
  lastBreathUpdateMs = 0;

  frames = kContinuousMaxFrames;
  frame = 0;
}

void BreathingExpression::onUpdate() {
  updateBreathPhase();
}

void BreathingExpression::control() {
  if (continuousControl()) {
    if (!autoTriggerEnabled) {
      // A transient frozen by the wisp hold would never finish its cycle and
      // never reach STOPPED for gcTransients(). Painting stays suppressed via
      // shouldAffectBuffer(); only completion progress advances.
      if (animationState == PLAYING || animationState == PLAYING_ONCE) {
        updateBreathPhase();
      }
      return;
    }
    // Recompute phase from the current time on resume; a multi-cycle wisp
    // hold would otherwise add one huge phaseIncrement and skip palette colors.
    lastBreathUpdateMs = 0;
    return;
  }

  if (animationState == PLAYING || animationState == PLAYING_ONCE) {
    onUpdate();
  }
}

void BreathingExpression::draw() {
  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  const uint16_t zoneSize = zone_.size();
  if (zoneSize == 0 || !fb || fb->pixelCount == 0) {
    if (autoTriggerEnabled) frame = rewindBeforeExhaust(frame, frames);
    nextFrame();
    return;
  }
  const int end = std::min(static_cast<int>(zone_.posMax) + 1, static_cast<int>(fb->pixelCount));
  for (int i = static_cast<int>(zone_.posMin); i < end; ++i) {
    const uint16_t off = static_cast<uint16_t>(i - zone_.posMin);
    const uint16_t band =
        static_cast<uint16_t>(static_cast<uint32_t>(off) * usableSections_ / zoneSize);
    float phase = breathPhase + sectionOrder_[band] * kBreathStaggerFrac;
    if (phase >= 1.0f) phase -= 1.0f;
    const float breath = (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
    const float intensity = applyEasing(easing_, breath);
    const uint32_t pIntensity = static_cast<uint32_t>(intensity * 100.0f);
    // Vignette over a region 2px wider with the offset shifted in by one, so
    // the darkest taper step lands off-screen and the outermost real pixel
    // reads the brighter second step.
    const uint32_t pct =
        pIntensity * edgeTaper(off + 1, zoneSize + 2, kBreathTaperWidth, TaperCurve::Quadratic) / 100u;
    if (pct >= 100u) {
      fb->buffer[i] = targetColor;
    } else {
      fb->buffer[i] = mixColorLinear(fb->buffer[i], targetColor,
                                     computeLinearFactor(pct, 100u));
    }
  }

  if (autoTriggerEnabled) frame = rewindBeforeExhaust(frame, frames);
  nextFrame();
}

}  // namespace lamp
