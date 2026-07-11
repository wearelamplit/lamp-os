#include "expressions/glitchy/glitchy_expression.hpp"
#include <Arduino.h>
#include "util/fade.hpp"

namespace lamp {

namespace {
// Glitchy always blends at a constant 95%. easeLinear's LUT stores index*511,
// and 95% maps to index 95*511/100 == 485, so the precomputed factor is
// linear[485] == 485*511 (see src/util/fade.cpp).
static constexpr uint32_t kFadeLutScale = 511;
static constexpr uint32_t kGlitchBlendLutIndex = 485;
static constexpr uint32_t kGlitchyLinearFactor = kGlitchBlendLutIndex * kFadeLutScale;

static constexpr ParamSpec kGlitchyParams[] = {
  {
    .key            = "count",
    .kind           = ParamKind::Int,
    .label          = "Points",
    .min            = 1,
    .max            = Bound::pixels(10),
    .def            = 1,
    .requiresZoning = true,
  },
  {
    .key            = "size",
    .kind           = ParamKind::Int,
    .label          = "Size",
    .min            = 1,
    .max            = Bound::pixels(),
    .def            = 1,
    .requiresZoning = true,
  },
};

static constexpr ExpressionDescriptor kGlitchyDescriptor{
  .id           = "glitchy",
  .name         = "Glitchy",
  .colors       = { .max = 8, .label = "Colors" },
  .interval     = RangeSpec{
    .min   = 60,
    .max   = 900,
    .step  = 30,
    .unit  = "s",
    .defLo = 60,
    .defHi = 900,
  },
  .duration     = RangeSpec{
    .min    = 30,
    .max    = 2000,
    .step   = 30,
    .unit   = "ms",
    .defLo  = 30,
    .defHi  = 120,
    .label  = "Glitch duration",
    .minKey = "durationMin",
    .maxKey = "durationMax",
  },
  .hasZone      = true,
  .zoneOptional = true,
  .params       = kGlitchyParams,
  .make         = &makeExpr<GlitchyExpression>,
};
}  // namespace

GlitchyExpression::GlitchyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
  allowedInHomeMode = false;  // Glitchy should not work in home mode
}

void GlitchyExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  glitchDurationMin = getParam(parameters, "durationMin") * kFrameRateHz / kMsPerSecond;
  glitchDurationMax = getParam(parameters, "durationMax") * kFrameRateHz / kMsPerSecond;
  if (glitchDurationMin < 1 || glitchDurationMin > 60) glitchDurationMin = 1;
  if (glitchDurationMax < 1 || glitchDurationMax > 60) glitchDurationMax = 3;
  if (glitchDurationMax < glitchDurationMin) glitchDurationMax = glitchDurationMin;

  const uint16_t window = windowSize();
  fullStrip_ = getParam(parameters, "fullStrip", 1) != 0;
  zone_ = Zone::fromParameters(parameters, window);
  points_ = Points::fromParameters(parameters, window, 1).count;
  size_ = parseSize(parameters, window, 1);
}

void GlitchyExpression::onTrigger() {
  saveBufferState();
  glitchColor = getRandomColor();

  // Randomly pick duration between min and max
  if (glitchDurationMin == glitchDurationMax) {
    frames = glitchDurationMin;
  } else {
    frames = rng.range(glitchDurationMin, glitchDurationMax);
  }

}

void GlitchyExpression::draw() {

  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  if (isLastFrame()) {
    fb->buffer = savedBuffer;
  } else if (fullStrip_) {
    for (int i = 0; i < fb->pixelCount; i++) {
      fb->buffer[i] = mixColorLinear(fb->buffer[i], glitchColor, kGlitchyLinearFactor);
    }
  } else {
    paintPoints_();
  }

  nextFrame();
}

void GlitchyExpression::paintPoints_() {
  if (fb->pixelCount == 0) return;
  const uint16_t clampedSize = zoneSpan(zone_, size_).clampedSize;
  if (clampedSize == 0) return;
  for (uint16_t p = 0; p < points_; ++p) {
    const uint16_t start = randomStartInZone(zone_, size_, rng);
    for (uint16_t i = start; i < start + clampedSize; ++i) {
      fb->buffer[i] = mixColorLinear(fb->buffer[i], glitchColor, kGlitchyLinearFactor);
    }
  }
}

const ExpressionDescriptor& GlitchyExpression::descriptor() {
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
