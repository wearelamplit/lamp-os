#include "expressions/breathing/breathing_expression.hpp"

#include <Arduino.h>
#include <algorithm>

#include "util/eased_scalar.hpp"
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
  configureEasing(parameters, 1);
  configureOpacity(parameters);
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

    // A live instance runs continuously with frames=100000 and
    // animationState=PLAYING, never naturally reaching STOPPED. A transient
    // preview stops after kPreviewCycles breaths so gcTransients() can reap it.
    if (previewCycleComplete()) {
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
  continuousControl();
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
  const float wispWeight = wispDimScale();
  for (int i = static_cast<int>(zone_.posMin); i < end; ++i) {
    const uint16_t off = static_cast<uint16_t>(i - zone_.posMin);
    const float breath = (breathPhase < 0.5f) ? breathPhase * 2.0f : 2.0f - breathPhase * 2.0f;
    const float intensity = applyEasing(easing_, breath);
    const uint32_t pIntensity = static_cast<uint32_t>(clamp01(intensity) * 100.0f);
    // Vignette over a region 2px wider with the offset shifted in by one, so
    // the darkest taper step lands off-screen and the outermost real pixel
    // reads the brighter second step.
    const uint32_t pct =
        pIntensity * edgeTaper(off + 1, zoneSize + 2, kBreathTaperWidth, TaperCurve::Quadratic) / 100u;
    const Color painted =
        (pct >= 100u)
            ? targetColor
            : mixColorLinear(fb->buffer[i], targetColor, computeLinearFactor(pct, 100u));
    fb->buffer[i] = mixColorWeight(fb->buffer[i], painted, wispWeight);
  }

  if (autoTriggerEnabled) frame = rewindBeforeExhaust(frame, frames);
  nextFrame();
}

}  // namespace lamp
