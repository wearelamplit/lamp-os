#include "expressions/spotty/spotty_expression.hpp"
#include <Arduino.h>
#include "util/fade.hpp"

namespace lamp {

namespace {
static constexpr ParamSpec kSpottyParams[] = {
  {
    .key   = "count",
    .kind  = ParamKind::Int,
    .label = "Points",
    .min   = 1,
    .max   = Bound::pixels(10),
    .def   = 3,
  },
  {
    .key   = "size",
    .kind  = ParamKind::Int,
    .label = "Size",
    .min   = 1,
    .max   = Bound::pixels(),
    .def   = 4,
  },
  {
    .key        = "spotSpeed",
    .kind       = ParamKind::Int,
    .label      = "Speed",
    .min        = 1,
    .max        = 10,
    .def        = 3,
    .unit       = "s",
    .invert     = true,
    .leftLabel  = "slow",
    .rightLabel = "fast",
  },
};
static constexpr ExpressionDescriptor kSpottyDescriptor{
  .id       = "spotty",
  .name     = "Spotty",
  .pausesWispOverride = true,
  .colors   = { .max = 8, .label = "Colors" },
  .interval = RangeSpec{
    .min   = 60,
    .max   = 900,
    .step  = 30,
    .unit  = "s",
    .defLo = 60,
    .defHi = 900,
  },
  .hasZone  = true,
  .params   = kSpottyParams,
  .make     = &makeExpr<SpottyExpression>,
};
}  // namespace

const ExpressionDescriptor& SpottyExpression::descriptor() {
  return kSpottyDescriptor;
}

SpottyExpression::SpottyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {}

void SpottyExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  const uint16_t window = windowSize();
  zone_ = Zone::fromParameters(parameters, window);
  points_ = Points::fromParameters(parameters, window, 3).count;
  size_ = parseSize(parameters, window, 4);

  uint32_t spotSpeed = getParam(parameters, "spotSpeed");
  if (spotSpeed < 1) spotSpeed = 1;
  if (spotSpeed > 10) spotSpeed = 10;
  lifeFrames_ = spotSpeed * kFrameRateHz;
}

void SpottyExpression::onTrigger() {
  saveBufferState();
  frame = 0;
  frames = lifeFrames_;

  const uint16_t regionSize = zone_.size();
  if (regionSize == 0 || !fb || fb->pixelCount == 0) {
    pointPositions_.clear();
    pointColors_.clear();
    return;
  }

  pointPositions_.resize(points_);
  pointColors_.resize(points_);

  for (uint16_t p = 0; p < points_; ++p) {
    pointPositions_[p] = randomStartInZone(zone_, size_, rng);
    pointColors_[p] = getRandomColor();
  }
}

void SpottyExpression::draw() {
  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  if (isLastFrame()) {
    fb->buffer = savedBuffer;
    nextFrame();
    return;
  }

  const uint32_t third = frames / 3;
  uint32_t blendPercent;
  if (third == 0) {
    blendPercent = 100;
  } else if (frame < third) {
    blendPercent = frame * 100 / third;
  } else if (frame < 2 * third) {
    blendPercent = 100;
  } else {
    const uint32_t outStart = 2 * third;
    const uint32_t outLen = (frames > outStart) ? (frames - outStart) : 1;
    const uint32_t elapsed = frame - outStart;
    blendPercent = (elapsed < outLen) ? (outLen - elapsed) * 100 / outLen : 0;
  }

  const uint32_t factor = computeLinearFactor(blendPercent, 100);

  const uint16_t regionSize = zone_.size();
  if (regionSize == 0 || !fb || pointPositions_.empty()) {
    nextFrame();
    return;
  }

  const uint16_t clampedSize = (size_ < regionSize) ? size_ : regionSize;
  const uint16_t pixMax = static_cast<uint16_t>(fb->pixelCount - 1);
  const uint16_t ptCount = static_cast<uint16_t>(pointPositions_.size());

  for (uint16_t p = 0; p < points_ && p < ptCount; ++p) {
    const uint16_t start = pointPositions_[p];
    for (uint16_t i = start; i < start + clampedSize; ++i) {
      if (i > pixMax) break;
      fb->buffer[i] = mixColorLinear(savedBuffer[i], pointColors_[p], factor);
    }
  }

  nextFrame();
}

}  // namespace lamp
