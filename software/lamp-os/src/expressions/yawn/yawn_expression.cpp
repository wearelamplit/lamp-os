#include "expressions/yawn/yawn_expression.hpp"

#include <algorithm>
#include <cstring>

#include "expressions/param_utils.hpp"
#include "expressions/primitives.hpp"
#include "util/fade.hpp"
#include "util/uptime.hpp"

namespace lamp {

namespace {
constexpr ExpressionDescriptor kYawnDescriptor =
    withMake(kYawnDescriptorData, &makeExpr<YawnExpression>);

// `key` clamped to its ParamSpec's range, so a serial or app value outside
// the slider can't stretch the timeline.
uint32_t clampedParam(const ExpressionParams& parameters, const char* key) {
  for (const ParamSpec& spec : kYawnParams) {
    if (std::strcmp(spec.key, key) != 0) continue;
    return std::clamp<uint32_t>(getParam(parameters, key), static_cast<uint32_t>(spec.min),
                                static_cast<uint32_t>(spec.max.v));
  }
  return 0;
}

float percent(const ExpressionParams& parameters, const char* key) {
  return static_cast<float>(clampedParam(parameters, key)) / 100.0f;
}
}  // namespace

YawnExpression::YawnExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {}

const ExpressionDescriptor& YawnExpression::classDescriptor() { return kYawnDescriptor; }
const ExpressionDescriptor& YawnExpression::descriptor() const { return kYawnDescriptor; }

void YawnExpression::configureFromParameters(const ExpressionParams& parameters) {
  shape_.durationMs = clampedParam(parameters, kYawnDurationKey);
  shape_.inhaleMs = kYawnInhaleMs;
  shape_.holdMs = clampedParam(parameters, kYawnHoldKey);
  shape_.recoverMs = clampedParam(parameters, kYawnRecoverKey);
  shape_.lift = percent(parameters, kYawnLiftKey);
  shape_.depth = percent(parameters, kYawnDepthKey);
  warmth_ = percent(parameters, kYawnWarmthKey);
  lagMs_ = clampedParam(parameters, kYawnLagKey);
  configureOpacity(parameters);
}

bool YawnExpression::shouldAffectBuffer() {
  return shouldAffectBuffer(fb) || (above_ && shouldAffectBuffer(above_));
}

void YawnExpression::onTrigger() {
  startMs_ = uptimeMs();
  frames = kContinuousMaxFrames;
  frame = 0;
}

void YawnExpression::draw() {
  const uint32_t elapsed = uptimeSince(startMs_);
  const uint32_t lag = above_ ? lagMs_ : 0;
  if (elapsed >= shape_.durationMs + lag) {
    frames = frame + 1;
  } else {
    if (above_) yawnSurface(above_, elapsed);
    if (elapsed >= lag) yawnSurface(fb, elapsed - lag);
    frame = rewindBeforeExhaust(frame, frames);
  }
  nextFrame();
}

void YawnExpression::yawnSurface(FrameBuffer* surface, uint32_t elapsedMs) {
  if (!surface || surface->pixelCount == 0 || !shouldAffectBuffer(surface)) return;
  const float level = yawnLevel(shape_, elapsedMs);
  const YawnGains gains = yawnGains(level, warmth_ * yawnWarmWeight(level, shape_.depth));
  const float weight = wispDimScale(surface);
  for (uint16_t i = 0; i < surface->pixelCount; ++i) {
    Color& px = surface->buffer[i];
    px = mixColorWeight(px, yawnPixel(px, gains), weight);
  }
}

}  // namespace lamp
