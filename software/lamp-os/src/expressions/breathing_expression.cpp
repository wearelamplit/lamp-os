#include "./breathing_expression.hpp"

#include <Arduino.h>
#include <algorithm>
#include <cmath>

#include "../util/fade.hpp"

namespace lamp {

// Large frame count to keep animation running continuously
static constexpr uint32_t BREATHING_MAX_FRAMES = 100000;

BreathingExpression::BreathingExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
  isExclusive = false;  // This can run and blend with other things
  allowedInHomeMode = true;
}

void BreathingExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  // Extract breath speed parameter (in seconds, convert to ms)
  auto itSpeed = parameters.find("breathSpeed");
  uint32_t breathSpeed = (itSpeed != parameters.end()) ? itSpeed->second : 10;
  breathSpeedMs = breathSpeed * 1000;

  // Color change intervals are set by base Expression::configure() in intervalMinMs/MaxMs
  // No need to read them from parameters

  targetColor = colors[0];
}

void BreathingExpression::updateBreathPhase() {
  uint32_t currentMs = millis();

  if (lastBreathUpdateMs == 0) {
    lastBreathUpdateMs = currentMs;
    return;
  }

  uint32_t deltaMs = currentMs - lastBreathUpdateMs;

  // Update phase based on breath speed
  // breathSpeedMs is the total cycle time, so increment is deltaMs / breathSpeedMs
  float phaseIncrement = static_cast<float>(deltaMs) / static_cast<float>(breathSpeedMs);
  breathPhase += phaseIncrement;

  // Wrap phase to stay in 0-1 range and advance color when cycle completes
  if (breathPhase >= 1.0f) {
    breathPhase -= 1.0f;

    // Advance to next color if we have multiple colors
    if (colors.size() > 1) {
      if (cyclingForward) {
        currentColorIndex++;
        // If we reached the last color, switch to backward
        if (currentColorIndex >= colors.size() - 1) {
          currentColorIndex = colors.size() - 1;
          cyclingForward = false;
        }
      } else {
        // Going backward
        if (currentColorIndex == 0) {
          // At first color, switch to forward
          cyclingForward = true;
          currentColorIndex = 1;
        } else {
          currentColorIndex--;
        }
      }

      // Update target color
      targetColor = colors[currentColorIndex];
    }
  }

  lastBreathUpdateMs = currentMs;
}

void BreathingExpression::onTrigger() {
  // Initialize breath state
  breathPhase = 0.0f;
  lastBreathUpdateMs = 0;

  // Initialize color cycling state
  currentColorIndex = 0;
  cyclingForward = true;

  // Set high frame count to keep running
  frames = BREATHING_MAX_FRAMES;
  frame = 0;

  // Start playing
  play();
}

void BreathingExpression::onUpdate() {
  // Always update breath phase
  updateBreathPhase();
}

void BreathingExpression::control() {
  // For breathing, we want to always be running (no interval-based triggering)
  // If stopped, trigger immediately to start
  if (animationState == STOPPED) {
    trigger();
  }

  // Per-frame updates during animation
  if (animationState == PLAYING || animationState == PLAYING_ONCE) {
    onUpdate();
  }
}

void BreathingExpression::draw() {
  // Pause if an exclusive behavior is running
  if (shouldPause()) return;

  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  float intensity = 0.5f - 0.5f * cosf(breathPhase * 2.0f * static_cast<float>(M_PI));
  uint32_t breathIntensity = static_cast<uint32_t>(intensity * 100.0f);

  for (int i = 0; i < fb->pixelCount; i++) {
    fb->buffer[i] = fadeLinear(fb->buffer[i], targetColor, 100, breathIntensity);
  }

  nextFrame();
}

}  // namespace lamp
