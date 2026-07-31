#include "expressions/shimmer/shimmer_expression.hpp"

#include <Arduino.h>
#include <algorithm>

#include "expressions/primitives.hpp"
#include "util/fade.hpp"

namespace lamp {

namespace {
constexpr ExpressionDescriptor kShimmerDescriptor =
    withMake(kShimmerDescriptorData, &makeExpr<ShimmerExpression>);
constexpr uint32_t kWindStepFactor = 3;
}  // namespace

const ExpressionDescriptor& ShimmerExpression::classDescriptor() {
  return kShimmerDescriptor;
}

const ExpressionDescriptor& ShimmerExpression::descriptor() const {
  return kShimmerDescriptor;
}

ShimmerExpression::ShimmerExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {}

void ShimmerExpression::configureFromParameters(
    const std::map<std::string, uint32_t>& parameters) {
  const uint16_t window = windowSize();
  zone_ = resolveZone(parameters, window);

  uint32_t fire = getParam(parameters, "fire");
  if (fire > 3) fire = 3;
  style_ = fireStyle(fire);

  configureOpacity(parameters);
}

void ShimmerExpression::onTrigger() {
  frame = 0;
  frames = kContinuousMaxFrames;
  lastUpdateMs_ = 0;
  windOffset_ = 0.0f;
  windTarget_ = 0.0f;
  nextGustMs_ = 0;

  cells_.clear();
  const uint16_t regionSize = zone_.size();
  if (regionSize == 0 || !fb || fb->pixelCount == 0) return;

  cells_.resize(regionSize);
  for (Cell& c : cells_) {
    c.heat = style_.restLevel;
    c.target = rollHeatTarget(style_, rng);
  }
}

void ShimmerExpression::advanceWind(uint32_t nowMs, uint32_t deltaMs) {
  if (style_.windAmp <= 0.0f) {
    windOffset_ = 0.0f;
    return;
  }
  if (nextGustMs_ == 0 || timeReached(nowMs, nextGustMs_)) {
    windTarget_ = rollWindTarget(style_, rng);
    nextGustMs_ = nextGustAt(nowMs, style_, rng);
  }
  windOffset_ = approachHeat(windOffset_, windTarget_, deltaMs,
                             style_.stepMs * kWindStepFactor);
}

void ShimmerExpression::control() {
  continuousControl();
}

void ShimmerExpression::draw() {
  const uint32_t nowMs = millis();
  const uint32_t deltaMs = (lastUpdateMs_ == 0) ? 0 : nowMs - lastUpdateMs_;
  lastUpdateMs_ = nowMs;

  const bool paint = shouldAffectBuffer() && zone_.size() > 0 && fb &&
                     fb->pixelCount > 0 && !cells_.empty();

  advanceWind(nowMs, deltaMs);
  for (Cell& c : cells_) {
    c.heat = approachHeat(c.heat, c.target, deltaMs, style_.stepMs);
    if (std::abs(c.target - c.heat) < 0.02f) {
      c.target = rollHeatTarget(style_, rng);
    }
  }

  if (paint) {
    const std::vector<Color>& palette =
        getColors().empty() ? defaultFireRamp() : getColors();
    const float wispWeight = wispDimScale();
    // cells_ is sized on trigger, zone_ on configure; bound the index so a
    // future in-place reconfigure that grows the zone can't read past cells_.
    const int end = std::min({static_cast<int>(zone_.posMax) + 1,
                              static_cast<int>(fb->pixelCount),
                              static_cast<int>(zone_.posMin) +
                                  static_cast<int>(cells_.size())});
    for (int i = static_cast<int>(zone_.posMin); i < end; ++i) {
      const Cell& c = cells_[static_cast<uint16_t>(i - zone_.posMin)];
      const float heatR = clampUnit(c.heat + windOffset_);
      const Color color = sampleGradient(palette, heatR);
      const uint32_t pct =
          static_cast<uint32_t>(heatBrightness(heatR, kShimmerMinBright) * 100.0f);
      const Color painted =
          (pct >= 100u)
              ? color
              : mixColorLinear(fb->buffer[i], color, computeLinearFactor(pct, 100u));
      fb->buffer[i] = mixColorWeight(fb->buffer[i], painted, wispWeight);
    }
  }

  frame = rewindBeforeExhaust(frame, frames);
  nextFrame();
}

}  // namespace lamp
