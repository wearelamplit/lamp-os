#include "expressions/pulse/pulse_expression.hpp"

#include <Arduino.h>
#include <algorithm>
#include <cmath>

#include "util/fade.hpp"

namespace lamp {

namespace {
constexpr ExpressionDescriptor kPulseDescriptor =
    withMake(kPulseDescriptorData, &makeExpr<PulseExpression>);
}  // namespace

PulseExpression::PulseExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
}

void PulseExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  const uint16_t window = windowSize();
  zone_ = resolveZone(parameters, window);

  uint32_t pulseSpeed = getParam(parameters, "pulseSpeed");
  const uint16_t span = zone_.size() > 0 ? zone_.size() : window;
  pulseSpeedMs = std::max<uint32_t>(100, (pulseSpeed * kMsPerSecond) / span);

  const uint16_t sizePercent = static_cast<uint16_t>(getParam(parameters, "size", 40));
  pulseWidth = pulseWidthFromPercent(sizePercent, span);
  pulseColor = firstColorOr(kSafeFallbackColor);

  easing_ = static_cast<Easing>(getParam(parameters, "easing", 0));
  loopContinuous_ = getParam(parameters, kLoopParamKey, 0) != 0;
}

uint32_t PulseExpression::calculateBlendFactor(int pixelIndex) const {
  float distance = std::abs(static_cast<float>(pixelIndex) - wavePosition);

  // No blend if outside pulse width
  if (distance > pulseWidth) {
    return 0;
  }

  // Special case: if very close to center (within 0.5), give full strength
  if (distance < 0.5f) {
    return 100;
  }

  // Simplified quadratic falloff for performance
  // Avoids expensive exp() calculation
  float normalizedDist = distance / static_cast<float>(pulseWidth);
  float factor = 1.0f - (normalizedDist * normalizedDist);  // Quadratic falloff
  factor = std::max(0.0f, factor);  // Clamp to 0

  // Return as integer 0-100 (percentage for fadeLinear)
  return static_cast<uint32_t>(factor * 100.0f);
}

void PulseExpression::updateWavePosition() {
  uint32_t currentMs = millis();

  if (lastUpdateMs == 0) {
    lastUpdateMs = currentMs;
    return;
  }

  // Cap deltaMs so a long pause before re-entry doesn't teleport the wave
  // across the span before any pixels are drawn.
  uint32_t deltaMs = std::min(currentMs - lastUpdateMs, (uint32_t)100);
  lastUpdateMs = currentMs;

  // Advance linear phase at pulseSpeedMs per pixel, normalized to the span.
  const float dt = static_cast<float>(deltaMs) /
                   (static_cast<float>(pulseSpeedMs) * travelSpan_);
  progress_ += dt * static_cast<float>(waveDirection);

  if (progress_ >= 1.0f) {
    progress_ = 1.0f;
    if (loopContinuous_) {
      waveDirection = -1;
      reachedFarEnd_ = true;
    }
  } else if (progress_ <= 0.0f) {
    progress_ = 0.0f;
    if (loopContinuous_) waveDirection = 1;
  }

  wavePosition = travelStart_ + applyEasing(easing_, progress_) * travelSpan_;
}

const ExpressionDescriptor& PulseExpression::classDescriptor() {
  return kPulseDescriptor;
}

const ExpressionDescriptor& PulseExpression::descriptor() const {
  return kPulseDescriptor;
}

void PulseExpression::selectNextColor() {
  // Capture into pulseColor on every trigger (not just when the palette
  // has 2+ entries) so receive-side cascade overrides land correctly even
  // for single-color invocations. getRandomColor() over a 1-element vector
  // returns that element, so the call is safe and idempotent.
  if (!colors.empty()) {
    pulseColor = getRandomColor();
  }
}

void PulseExpression::onTrigger() {
  if (loopContinuous_) {
    travelStart_ = static_cast<float>(zone_.posMin) + kContinuousTravelInset;
    const float travelEnd = static_cast<float>(zone_.posMax) - kContinuousTravelInset;
    travelSpan_ = std::max(1.0f, travelEnd - travelStart_);
  } else {
    travelStart_ = static_cast<float>(zone_.posMin) - static_cast<float>(pulseWidth);
    travelSpan_ = (static_cast<float>(zone_.posMax) + 2.0f * pulseWidth) - travelStart_;
  }
  progress_ = 0.0f;
  waveDirection = 1;
  reachedFarEnd_ = false;
  wavePosition = travelStart_;
  lastUpdateMs = 0;
  ebbStartMs_ = millis();
  selectNextColor();

  frames = kContinuousMaxFrames;
  frame = 0;
}

float PulseExpression::ebbInScale() const {
  if (!loopContinuous_) return 1.0f;
  const uint32_t elapsed = millis() - ebbStartMs_;
  if (elapsed >= kEbbInMs) return 1.0f;
  return static_cast<float>(elapsed) / static_cast<float>(kEbbInMs);
}

void PulseExpression::onUpdate() {
  updateWavePosition();
}

void PulseExpression::draw() {
  if (!fb || fb->pixelCount == 0) {
    nextFrame();
    return;
  }

  if (!shouldAffectBuffer()) {
    return;
  }

  const float ebb = ebbInScale();

  // blendFactor varies per pixel (distance to wave center), so no frame-level hoist.
  for (int i = zone_.posMin; i <= zone_.posMax; i++) {
    uint32_t blendFactor = calculateBlendFactor(i);
    if (ebb < 1.0f) blendFactor = static_cast<uint32_t>(blendFactor * ebb);

    if (blendFactor == 0) continue;  // Skip pixels with no blend
    if (blendFactor >= 100) {
      fb->buffer[i] = pulseColor;
      continue;
    }

    uint32_t factor = computeLinearFactor(blendFactor, 100u);
    fb->buffer[i] = mixColorLinear(fb->buffer[i], pulseColor, factor);
  }

  // Transit time is wall-clock; a tiny zone with a slow, wide wave can take
  // minutes. Trigger mode ends when the wave exits (progress complete).
  // Continuous ping-pongs forever when live, but a transient preview (Test,
  // autoTriggerEnabled false) does one out-and-back then stops so the GC reaps it.
  const bool triggerExit = !loopContinuous_ && progress_ >= 1.0f;
  const bool previewCycleDone =
      loopContinuous_ && !autoTriggerEnabled && reachedFarEnd_ && progress_ <= 0.0f;
  if (triggerExit || previewCycleDone) {
    frames = frame + 1;
  } else {
    frame = rewindBeforeExhaust(frame, frames);
  }
  nextFrame();
}

}  // namespace lamp
