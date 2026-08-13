#include "expressions/bloom/bloom_expression.hpp"

namespace lamp {

namespace {
constexpr ExpressionDescriptor kBloomDescriptor =
    withMake(kBloomDescriptorData, &makeExpr<BloomExpression>);
}  // namespace

BloomExpression::BloomExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {}

const ExpressionDescriptor& BloomExpression::classDescriptor() {
  return kBloomDescriptor;
}

const ExpressionDescriptor& BloomExpression::descriptor() const {
  return kBloomDescriptor;
}

void BloomExpression::onTrigger() {
  frames = kBloomFrames;
  frame = 0;
}

void BloomExpression::draw() {
  if (!fb || fb->pixelCount == 0) {
    nextFrame();
    return;
  }
  if (!shouldAffectBuffer()) return;

  const uint8_t s = bloomSwell(frame, frames);
  for (uint16_t i = 0; i < fb->pixelCount; ++i) {
    fb->buffer[i] = bloomWhiten(fb->buffer[i], s);
  }
  nextFrame();
}

}  // namespace lamp
