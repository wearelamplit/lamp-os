#include "pulse_expression.hpp"

#include <Arduino.h>
#include <algorithm>
#include <cmath>

#include "util/fade.hpp"

namespace lamp {

// Use a large frame count to let wave position determine animation end
// This prevents premature stopping based on frame count
static constexpr uint32_t PULSE_MAX_FRAMES = 10000;

PulseExpression::PulseExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
  allowedInHomeMode = true;
}

void PulseExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  // Extract pulse speed parameter with default value
  auto it = parameters.find("pulseSpeed");
  uint32_t pulseSpeed = (it != parameters.end()) ? it->second : 3;

  // Pulse-specific configuration
  // pulseSpeed is total travel time in seconds (1-10s)
  // Convert to ms per pixel: total_time_ms / pixel_count
  if (fb && fb->pixelCount > 0) {
    pulseSpeedMs = (pulseSpeed * 1000) / fb->pixelCount;
    pulseSpeedMs = std::max((uint32_t)100, pulseSpeedMs);  // Minimum 100ms per pixel
  } else {
    pulseSpeedMs = 100;  // Default fallback
  }

  // Pulse width constant
  static constexpr uint32_t PULSE_WIDTH = 15;
  pulseWidth = PULSE_WIDTH;

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

  // Why: cap deltaMs to protect against stale lastUpdateMs across long pauses.
  // updateWavePosition() is gated by shouldAffectBuffer() and the wisp-override
  // path, and the same lastUpdateMs persists through STOPPED → PLAYING
  // re-entries. Without the cap, the first tick after a long gap would
  // teleport wavePosition past the (pixelCount + 2*pulseWidth) end-of-strip
  // stop check in one step, flipping the animation to STOPPED before any
  // pixels were drawn at the new position. The compositor's
  // MINIMUM_FRAME_DRAW_TIME_MS already bounds normal frame spacing, so this
  // ceiling only matters on the pathological-gap path.
  uint32_t deltaMs = std::min(currentMs - lastUpdateMs, (uint32_t)100);

  // Calculate how far to move based on speed
  float pixelsToMove = static_cast<float>(deltaMs) / static_cast<float>(pulseSpeedMs);

  wavePosition += pixelsToMove * waveDirection;

  // Continue moving wave past the end of the strip
  // Animation will stop when frame >= frames via nextFrame()
  // Wave should reach position (pixelCount + pulseWidth) before stopping

  lastUpdateMs = currentMs;
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
  // Reset wave to start position
  wavePosition = -static_cast<float>(pulseWidth);  // Start just off the strip
  waveDirection = 1;  // Always move forward
  lastUpdateMs = 0;
  selectNextColor();

  // Set frames high enough that wave position will determine when to stop
  // The animation will complete when wave reaches pixelCount + (2 * pulseWidth)
  frames = PULSE_MAX_FRAMES;
  frame = 0;

}

void PulseExpression::onUpdate() {
  // Always update wave position to ensure smooth fade-out
  updateWavePosition();
}

void PulseExpression::draw() {
  // For pulse, we need to continue drawing even when "stopped" to allow fade-out
  // Only skip if we shouldn't affect this buffer based on target
  if (!shouldAffectBuffer() && animationState != STOPPED) {
    return;
  }

  // Continue updating wave position even when STOPPED to complete fade-out
  if (animationState == STOPPED && wavePosition <= fb->pixelCount + (2 * pulseWidth)) {
    updateWavePosition();
  }

  // Apply pulse effect. blendFactor is per-pixel (depends on distance to wave
  // center), so the audit's "hoist factor once per frame" pattern doesn't fit
  // here — but we can still avoid the 4 channel function calls per pixel by
  // inlining the easeLinear math via mixColorLinear. End-clamp short-circuit
  // mirrors easeLinear()'s `currentStep >= duration` branch (returns end).
  for (int i = 0; i < fb->pixelCount; i++) {
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

  // Check if wave has completely cleared the strip
  // Wave extends pulseWidth on both sides, so center needs to be at pixelCount + (2 * pulseWidth)
  // for the trailing edge to clear pixelCount
  if (wavePosition > fb->pixelCount + (2 * pulseWidth)) {
    // Wave has completely passed, safe to stop
    if (animationState != STOPPED) {
      animationState = STOPPED;
      frame = 0;
      currentLoop += 1;
    }
  }

}

}  // namespace lamp