#include "expressions/glitchy/glitchy_expression.hpp"
#include <Arduino.h>
#include <algorithm>
#include <array>
#include "util/fade.hpp"

namespace lamp {

namespace {
// Glitchy always blends at a constant 95%. easeLinear's LUT stores index*511,
// and 95% maps to index 95*511/100 == 485, so the precomputed factor is
// linear[485] == 485*511 (see src/util/fade.cpp).
static constexpr uint32_t kFadeLutScale = 511;
static constexpr uint32_t kGlitchBlendLutIndex = 485;
static constexpr uint32_t kGlitchyLinearFactor = kGlitchBlendLutIndex * kFadeLutScale;

constexpr ExpressionDescriptor kGlitchyDescriptor =
    withMake(kGlitchyDescriptorData, &makeExpr<GlitchyExpression>);
}  // namespace

GlitchyExpression::GlitchyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
}

void GlitchyExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  const uint32_t durMin = static_cast<uint32_t>(kGlitchyDescriptorData.duration->min);
  const uint32_t durMax = static_cast<uint32_t>(kGlitchyDescriptorData.duration->max);
  glitchDurationMinMs = std::clamp(getParam(parameters, "durationMin"), durMin, durMax);
  glitchDurationMaxMs = std::clamp(getParam(parameters, "durationMax"),
                                   glitchDurationMinMs, durMax);

  zone_ = resolveZone(parameters, windowSize());
  scatter_ = std::min<uint32_t>(getParam(parameters, "scatter", kGlitchScatterMax),
                                kGlitchScatterMax);
}

void GlitchyExpression::onTrigger() {
  saveBufferState();
  glitchColor = getRandomColor();
  glitchEndMs = millis() + rng.range(glitchDurationMinMs, glitchDurationMaxMs);
  painted_ = false;
  frames = kContinuousMaxFrames;
  frame = 0;
}

void GlitchyExpression::draw() {
  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  if (painted_ && timeReached(millis(), glitchEndMs)) {
    fb->buffer = savedBuffer;
    // Cap the counter; nextFrame() flips PLAYING_ONCE to STOPPED and
    // onComplete may chain a follow-up glitch.
    frames = frame + 1;
  } else {
    // Repaint from the saved background each frame so the scatter re-rolls into
    // a steady density instead of accumulating.
    fb->buffer = savedBuffer;
    paintGlitch_();
    painted_ = true;
  }

  nextFrame();
}

void GlitchyExpression::paintGlitch_() {
  if (zone_.size() == 0) return;

  if (scatter_ == 0) {
    for (uint16_t i = zone_.posMin; i <= zone_.posMax; ++i)
      fb->buffer[i] = mixColorLinear(fb->buffer[i], glitchColor, kGlitchyLinearFactor);
    return;
  }

  const GlitchPlan plan = glitchBlockPlan(scatter_, zone_.size());
  if (plan.blocksWanted == 0) return;

  std::array<uint8_t, 256> slots;
  randomPermutation(slots, plan.slotCount, rng);
  for (uint16_t b = 0; b < plan.blocksWanted; ++b) {
    const uint16_t start =
        static_cast<uint16_t>(zone_.posMin + slots[b] * plan.grainPx);
    const uint16_t end = std::min<uint16_t>(start + plan.grainPx, zone_.posMax + 1);
    for (uint16_t i = start; i < end; ++i)
      fb->buffer[i] = mixColorLinear(fb->buffer[i], glitchColor, kGlitchyLinearFactor);
  }
}

const ExpressionDescriptor& GlitchyExpression::classDescriptor() {
  return kGlitchyDescriptor;
}

const ExpressionDescriptor& GlitchyExpression::descriptor() const {
  return kGlitchyDescriptor;
}

void GlitchyExpression::onComplete() {
  // ~33% chance to chain a second glitch 200-600 ms after this one
  // finishes, ignoring the configured intervalMin/Max. The base
  // class's scheduleNextTrigger() handles the next regular trigger
  // when this short-circuit doesn't fire.
  if (rng.range(0, 99) < 33) {
    nextTriggerMs = millis() + rng.range(200, 600);
  }
}

}  // namespace lamp
