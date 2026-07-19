#include "expressions/spotty/spotty_expression.hpp"
#include <Arduino.h>
#include <algorithm>
#include "expressions/spotty/spotty_math.hpp"
#include "util/fade.hpp"

namespace lamp {

namespace {
constexpr ExpressionDescriptor kSpottyDescriptor =
    withMake(kSpottyDescriptorData, &makeExpr<SpottyExpression>);
}  // namespace

const ExpressionDescriptor& SpottyExpression::classDescriptor() {
  return kSpottyDescriptor;
}

const ExpressionDescriptor& SpottyExpression::descriptor() const {
  return kSpottyDescriptor;
}

SpottyExpression::SpottyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {}

void SpottyExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  const uint16_t window = windowSize();
  zone_ = resolveZone(parameters, window);
  // Persisted params can exceed the descriptor caps; clamp on load.
  points_ = std::min(Points::fromParameters(parameters, window, 3).count, kSpottyMaxCount);
  size_ = std::min(parseSize(parameters, window, 3), kSpottyMaxSize);

  uint32_t spotSpeed = getParam(parameters, "spotSpeed");
  if (spotSpeed < 1) spotSpeed = 1;
  if (spotSpeed > 10) spotSpeed = 10;
  spotSpeed_ = static_cast<uint16_t>(spotSpeed);

  easing_ = static_cast<Easing>(getParam(parameters, "easing", 0));
}

void SpottyExpression::respawn(Spot& spot) {
  spot.pos = randomStartInZone(zone_, size_, rng);
  spot.color = getRandomColor();
  spot.ageMs = 0;
  const SpotLifeBounds bounds = spotLifeBounds(spotSpeed_);
  spot.lifeMs = rng.range(bounds.lo, bounds.hi);
}

void SpottyExpression::onTrigger() {
  frame = 0;
  frames = kContinuousMaxFrames;
  lastUpdateMs_ = 0;

  spots_.clear();
  if (zone_.size() == 0 || !fb || fb->pixelCount == 0) return;

  spots_.resize(points_);
  for (Spot& spot : spots_) {
    respawn(spot);
    // Transients (autoTriggerEnabled=false) skip the stagger so all spots run
    // one unison cycle and the instance reaches STOPPED for gcTransients().
    if (autoTriggerEnabled && spot.lifeMs > 1) {
      spot.ageMs = rng.range(0, spot.lifeMs - 1);
    }
  }
}

void SpottyExpression::control() {
  continuousControl();
}

void SpottyExpression::draw() {
  const uint32_t nowMs = millis();
  const uint32_t deltaMs = (lastUpdateMs_ == 0) ? 0 : nowMs - lastUpdateMs_;
  lastUpdateMs_ = nowMs;

  const uint16_t regionSize = zone_.size();
  // Spot ages advance on wall clock even while painting is suppressed (wisp
  // hold), so a frozen transient still reaches STOPPED for gcTransients()
  // instead of replaying its stale cycle when the wisp releases.
  const bool paint = shouldAffectBuffer() && regionSize > 0 && !spots_.empty();
  const uint16_t clampedSize = paint ? std::min(size_, regionSize) : 0;
  const uint16_t pixMax = paint ? static_cast<uint16_t>(fb->pixelCount - 1) : 0;

  bool allDone = true;
  for (Spot& spot : spots_) {
    if (paint) {
      const uint32_t blend = spotBlendPercent(spot.ageMs, spot.lifeMs, easing_);
      for (uint16_t k = 0; k < clampedSize; ++k) {
        const uint16_t i = static_cast<uint16_t>(spot.pos + k);
        if (i > pixMax) break;
        const uint32_t pct =
            blend * edgeTaper(k, clampedSize, clampedSize / 2, TaperCurve::Linear) / 100u;
        if (pct >= 100u) {
          fb->buffer[i] = spot.color;
        } else {
          fb->buffer[i] =
              mixColorLinear(fb->buffer[i], spot.color, computeLinearFactor(pct, 100u));
        }
      }
    }
    spot.ageMs += deltaMs;
    if (spot.ageMs >= spot.lifeMs) {
      if (autoTriggerEnabled) {
        respawn(spot);
      } else {
        spot.ageMs = spot.lifeMs;
      }
    }
    if (spot.ageMs < spot.lifeMs) allDone = false;
  }

  if (!autoTriggerEnabled && allDone) {
    animationState = STOPPED;
    lastCompletedLoop = currentLoop + 1;
  }

  if (autoTriggerEnabled) frame = rewindBeforeExhaust(frame, frames);
  nextFrame();
}

}  // namespace lamp
