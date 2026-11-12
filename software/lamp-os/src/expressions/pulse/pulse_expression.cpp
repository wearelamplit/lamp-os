#include "expressions/pulse/pulse_expression.hpp"

#include <Arduino.h>
#include <algorithm>
#include <cmath>

#include <lampos/clock.hpp>

#include "util/fade.hpp"

namespace lamp {

namespace {
constexpr ExpressionDescriptor kPulseDescriptor =
    withMake(kPulseDescriptorData, &makeExpr<PulseExpression>);
}  // namespace

bool PulseExpression::shouldAffectBuffer() {
  return shouldAffectBuffer(fb) || (above_ && shouldAffectBuffer(above_));
}

PulseExpression::PulseExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
}

void PulseExpression::configureFromParameters(const ExpressionParams& parameters) {
  const uint16_t window = windowSize();
  zone_ = resolveZone(parameters, window);
  lowerEnd_ = zone_.posMax;
  if (above_ && above_->pixelCount > 0) {
    const Zone upper = resolveZone(parameters, above_->pixelCount);
    aboveMin_ = upper.posMin;
    zone_.posMax = static_cast<uint16_t>(lowerEnd_ + upper.size());
  }
  down_ = getParam(parameters, "travel", 0) != 0;

  uint32_t pulseSpeed = getParam(parameters, "pulseSpeed");
  const uint16_t span = zone_.size() > 0 ? zone_.size() : window;
  // The speed floor is per surface: a joined strip crosses at the floor in the
  // time its longer surface takes alone.
  const uint16_t lowerSpan = static_cast<uint16_t>(lowerEnd_ + 1 - zone_.posMin);
  const uint32_t floorSpan = std::max<uint32_t>(lowerSpan, span - lowerSpan);
  pulseSpeedMs = std::max<uint32_t>(kPulseFloorMsPerPx * floorSpan / span,
                                    (pulseSpeed * kMsPerSecond) / span);

  const uint16_t sizePercent = pulseSizePercentFromStep(getParam(parameters, "size", 4));
  pulseWidth = pulseWidthFromPercent(sizePercent, span);
  pulseColor = firstColorOr(kSafeFallbackColor);

  configureEasing(parameters, 0);
  loopContinuous_ = getParam(parameters, kLoopParamKey, 0) != 0;
  configureOpacity(parameters);
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

  const uint32_t deltaMs = lampos::frameDeltaMs(currentMs, lastUpdateMs);
  lastUpdateMs = currentMs;

  // Advance linear phase at pulseSpeedMs per pixel, normalized to the span.
  const float dt = static_cast<float>(deltaMs) /
                   (static_cast<float>(pulseSpeedMs) * travelSpan_);
  progress_ += dt * static_cast<float>(waveDirection);

  if (progress_ >= 1.0f) {
    progress_ = 1.0f;
    if (loopContinuous_) {
      if (!firstEntranceDone_) {
        firstEntranceDone_ = true;
        travelStart_ = static_cast<float>(zone_.posMin);
        travelSpan_ = std::max(1.0f, static_cast<float>(zone_.posMax) - travelStart_);
      }
      waveDirection = -1;
      reachedFarEnd_ = true;
    }
  } else if (progress_ <= 0.0f) {
    progress_ = 0.0f;
    if (loopContinuous_) waveDirection = 1;
  }

  wavePosition = wavePositionFromProgress();
}

float PulseExpression::wavePositionFromProgress() const {
  if (loopContinuous_) {
    return travelStart_ + applyEasing(easing_, progress_) * travelSpan_;
  }
  const float linearPos = travelStart_ + progress_ * travelSpan_;
  const float lo = static_cast<float>(zone_.posMin);
  const float hi = static_cast<float>(zone_.posMax);
  if (hi <= lo || linearPos <= lo || linearPos >= hi) {
    return linearPos;
  }
  const float u = (linearPos - lo) / (hi - lo);
  return lo + applyEasing(easing_, u) * (hi - lo);
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
    // Leg 0 enters off-strip; updateWavePosition() repoints to the visible
    // edges once it lands, then ping-pongs there forever.
    travelStart_ = static_cast<float>(zone_.posMin) - static_cast<float>(pulseWidth);
    travelSpan_ = std::max(1.0f, static_cast<float>(zone_.posMax) - travelStart_);
  } else {
    travelStart_ = static_cast<float>(zone_.posMin) - static_cast<float>(pulseWidth);
    travelSpan_ = (static_cast<float>(zone_.posMax) + 2.0f * pulseWidth) - travelStart_;
  }
  progress_ = 0.0f;
  waveDirection = 1;
  reachedFarEnd_ = false;
  firstEntranceDone_ = false;
  wavePosition = travelStart_;
  lastUpdateMs = 0;
  selectNextColor();

  frames = kContinuousMaxFrames;
  frame = 0;
}

void PulseExpression::onUpdate() {
  updateWavePosition();
}

void PulseExpression::control() {
  if (!loopContinuous_) {
    Expression::control();
    return;
  }
  continuousControl();
  if (animationState == PLAYING || animationState == PLAYING_ONCE) {
    onUpdate();
  }
  // Pulse marks completion via frames=frame+1 in draw(), so the base
  // onComplete dispatch must run here to set lastCompletedLoop and let
  // gcTransients() reap a finished preview.
  if (animationState == STOPPED && currentLoop > lastCompletedLoop) {
    onComplete();
    lastCompletedLoop = currentLoop;
  }
}

void PulseExpression::draw() {
  if (!fb || fb->pixelCount == 0) {
    nextFrame();
    return;
  }

  const bool lowerLive = shouldAffectBuffer(fb);
  const bool upperLive = above_ && zone_.posMax > lowerEnd_ && shouldAffectBuffer(above_);
  if (!lowerLive && !upperLive) {
    return;
  }

  // blendFactor varies per pixel (distance to wave center), so no frame-level hoist.
  // Per-surface weights differ only once Pulse yields to the wisp (wispDimFloor < 1).
  const float lowerWeight = lowerLive ? wispDimScale(fb) : 0.0f;
  const float upperWeight = upperLive ? wispDimScale(above_) : 0.0f;
  // Base logical 0 is its top, so a base-only pulse walks its zone mirrored to
  // rise; the joined walk maps the base half itself.
  const bool mirrored = down_ != (!above_ && target == TARGET_BASE);
  for (int pos = zone_.posMin; pos <= zone_.posMax; pos++) {
    const int at = mirrored ? zone_.posMin + zone_.posMax - pos : pos;
    const bool upper = at > lowerEnd_;
    if (!(upper ? upperLive : lowerLive)) continue;
    uint32_t blendFactor = calculateBlendFactor(pos);
    if (blendFactor == 0) continue;  // Skip pixels with no blend
    Color& px = !above_ ? fb->buffer[at]
                : upper  ? above_->buffer[aboveMin_ + (at - lowerEnd_ - 1)]
                         : fb->buffer[zone_.posMin + lowerEnd_ - at];
    const Color painted =
        (blendFactor >= 100)
            ? pulseColor
            : mixColorLinear(px, pulseColor, computeLinearFactor(blendFactor, 100u));
    px = mixColorWeight(px, painted, upper ? upperWeight : lowerWeight);
  }

  // Transit time is wall-clock; a tiny zone with a slow, wide wave can take
  // minutes. Trigger mode ends when the wave exits (progress complete).
  // Continuous ping-pongs forever when live; a transient preview stops after
  // kPreviewCycles out-and-back legs so the GC reaps it. One leg completes on
  // the return to the near edge.
  const bool triggerExit = !loopContinuous_ && progress_ >= 1.0f;
  const bool cycleReturned = loopContinuous_ && reachedFarEnd_ && progress_ <= 0.0f;
  const bool previewCycleDone = cycleReturned && previewCycleComplete();
  if (triggerExit || previewCycleDone) {
    frames = frame + 1;
  } else {
    frame = rewindBeforeExhaust(frame, frames);
  }
  nextFrame();
}

}  // namespace lamp
