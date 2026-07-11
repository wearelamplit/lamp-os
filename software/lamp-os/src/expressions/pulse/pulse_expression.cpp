#include "expressions/pulse/pulse_expression.hpp"

#include <Arduino.h>
#include <algorithm>
#include <cmath>

#include "util/fade.hpp"

namespace lamp {

namespace {
static constexpr ParamSpec kPulseParams[] = {
  {
    .key        = "pulseSpeed",
    .kind       = ParamKind::Int,
    .label      = "Pulse speed",
    .min        = 1,
    .max        = 10,
    .def        = 3,
    .unit       = "s",
    .invert     = true,
    .leftLabel  = "slow",
    .rightLabel = "fast",
  },
  {
    .key   = "size",
    .kind  = ParamKind::Int,
    .label = "Size",
    .min   = 1,
    .max   = Bound::pixels(),
    .def   = 15,
  },
};
static constexpr ExpressionDescriptor kPulseDescriptor{
  .id       = "pulse",
  .name     = "Pulse",
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
  .params   = kPulseParams,
  .make     = &makeExpr<PulseExpression>,
};
}  // namespace

// Use a large frame count to let wave position determine animation end
// This prevents premature stopping based on frame count
static constexpr uint32_t PULSE_MAX_FRAMES = 10000;

PulseExpression::PulseExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
  allowedInHomeMode = true;
}

void PulseExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  const uint16_t window = windowSize();
  zone_ = Zone::fromParameters(parameters, window);

  uint32_t pulseSpeed = getParam(parameters, "pulseSpeed");
  const uint16_t span = zone_.size() > 0 ? zone_.size() : window;
  pulseSpeedMs = std::max<uint32_t>(100, (pulseSpeed * kMsPerSecond) / span);

  pulseWidth = parseSize(parameters, window, 15);
  pulseColor = firstColorOr(kSafeFallbackColor);
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

  // Cap deltaMs so a long pause before re-entry doesn't teleport wavePosition
  // past zone_.posMax before any pixels are drawn.
  uint32_t deltaMs = std::min(currentMs - lastUpdateMs, (uint32_t)100);

  // Calculate how far to move based on speed
  float pixelsToMove = static_cast<float>(deltaMs) / static_cast<float>(pulseSpeedMs);

  wavePosition += pixelsToMove * waveDirection;

  // Continue moving wave past the end of the strip
  // Animation will stop when frame >= frames via nextFrame()
  // Wave should reach position (pixelCount + pulseWidth) before stopping

  lastUpdateMs = currentMs;
}

const ExpressionDescriptor& PulseExpression::descriptor() {
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
  wavePosition = static_cast<float>(zone_.posMin) - static_cast<float>(pulseWidth);
  waveDirection = 1;  // Always move forward
  lastUpdateMs = 0;
  selectNextColor();

  // Set frames high enough that wave position will determine when to stop
  frames = PULSE_MAX_FRAMES;
  frame = 0;

}

void PulseExpression::onUpdate() {
  // Always update wave position to ensure smooth fade-out
  updateWavePosition();
}

void PulseExpression::draw() {
  if (!fb || fb->pixelCount == 0) {
    nextFrame();
    return;
  }

  // For pulse, we need to continue drawing even when "stopped" to allow fade-out
  // Only skip if we shouldn't affect this buffer based on target
  if (!shouldAffectBuffer() && animationState != STOPPED) {
    return;
  }

  if (animationState == STOPPED && wavePosition <= zone_.posMax + (2 * pulseWidth)) {
    updateWavePosition();
  }

  // blendFactor varies per pixel (distance to wave center), so no frame-level hoist.
  for (int i = zone_.posMin; i <= zone_.posMax; i++) {
    uint32_t blendFactor = calculateBlendFactor(i);

    if (blendFactor == 0) continue;  // Skip pixels with no blend
    if (blendFactor >= 100) {
      fb->buffer[i] = pulseColor;
      continue;
    }

    uint32_t factor = computeLinearFactor(blendFactor, 100u);
    fb->buffer[i] = mixColorLinear(fb->buffer[i], pulseColor, factor);
  }

  // Advance animation frame
  nextFrame();

  if (wavePosition > zone_.posMax + (2 * pulseWidth)) {
    if (animationState != STOPPED) {
      animationState = STOPPED;
      frame = 0;
      currentLoop += 1;
    }
  }

}

}  // namespace lamp
