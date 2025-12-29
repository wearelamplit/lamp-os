#include "./shifty_expression.hpp"

#include <Arduino.h>

#include <algorithm>

#include "../util/fade.hpp"
#include "./expression_manager.hpp"

namespace lamp {

ShiftyExpression::ShiftyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
}

void ShiftyExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  // Extract parameters with default values
  auto itMin = parameters.find("shiftDurationMin");
  uint32_t shiftDurationMin = (itMin != parameters.end()) ? itMin->second : 300;  // 5 minutes

  auto itMax = parameters.find("shiftDurationMax");
  uint32_t shiftDurationMax = (itMax != parameters.end()) ? itMax->second : 600;  // 10 minutes

  auto itFade = parameters.find("fadeDuration");
  uint32_t fadeDuration = (itFade != parameters.end()) ? itFade->second : 60;  // 60 seconds

  // Apply configuration
  shiftDurationMinMs = shiftDurationMin * 1000;
  shiftDurationMaxMs = shiftDurationMax * 1000;
  fadeDurationFrames = fadeDuration * 30;  // Convert seconds to frames at 30fps

  // If no colors configured, use current buffer colors as default
  if (colors.empty() && fb && fb->pixelCount > 0) {
    colors.push_back(fb->buffer[0]);
  }
}

void ShiftyExpression::startShift() {
  // Pick a random color to shift to
  if (!colors.empty()) {
    shiftedColor = getRandomColor();
  } else {
    // Fallback to white if no colors configured
    shiftedColor = Color(255, 255, 255, 255);
  }

  // For fade start, use current buffer state
  fadeStartColors = fb->buffer;

  // Set target to the shifted color for all pixels
  fadeTargetColors.assign(fb->pixelCount, shiftedColor);

  // Set animation parameters
  frames = fadeDurationFrames;
  frame = 0;
  state = FADING_TO_PALETTE;

  // Determine how long to stay shifted
  currentShiftDurationMs = getRandomShiftDuration();
  shiftStartMs = millis();

  // Animation will be started by base class trigger()
}

void ShiftyExpression::startUnshift() {
  // Set up fade back to original
  // Start from the shifted color (what we're currently showing)
  fadeStartColors.assign(fb->pixelCount, shiftedColor);
  fadeTargetColors = savedBuffer;

  // Reset animation parameters for fade back
  frames = fadeDurationFrames;
  frame = 0;
  state = FADING_BACK;

  // Keep animation running (don't change animationState, it's already PLAYING_ONCE)
}

uint32_t ShiftyExpression::getRandomShiftDuration() {
  std::uniform_int_distribution<uint32_t> dist(shiftDurationMinMs,
                                               shiftDurationMaxMs);
  return dist(rng);
}

void ShiftyExpression::onTrigger() {
  // If we're already animating, cancel and start fresh
  // This allows manual triggers to work at any time
  if (state != IDLE) {
    // Reset to idle state
    state = IDLE;
  }

  startShift();
}

void ShiftyExpression::onUpdate() {
  switch (state) {
    case FADING_TO_PALETTE:
      // Check if fade is complete
      if (isLastFrame()) {
        state = SHIFTED;
        shiftStartMs = millis();
        // Extend animation to last through the shift hold period
        // Add enough frames to cover the shift duration
        frames = frame + (currentShiftDurationMs / 1000.0f) * 30;
      }
      break;

    case SHIFTED:
      // Check if it's time to unshift
      if (millis() - shiftStartMs > currentShiftDurationMs) {
        startUnshift();
      }
      break;

    case FADING_BACK:
      // Check if fade back is complete
      if (isLastFrame()) {
        state = IDLE;
        // Animation will naturally stop after this
      }
      break;

    default:
      break;
  }
}

void ShiftyExpression::onComplete() {
  // Always trigger glitch on unshift if glitchy is available and we just finished fading back
  if (state == IDLE) {
    if (auto* manager = getGlobalExpressionManager()) {
      manager->triggerExpression("glitchy");
    }
  }
}

void ShiftyExpression::draw() {
  // Pause if an exclusive behavior is running
  if (shouldPause()) return;

  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  switch (state) {
    case FADING_TO_PALETTE:
    case FADING_BACK: {
      // Interpolate each pixel using integer-based fade
      for (int i = 0; i < fb->pixelCount; i++) {
        fb->buffer[i] = fadeLinear(fadeStartColors[i], fadeTargetColors[i], frames, frame);
      }
      break;
    }

    case SHIFTED:
      // Display the shifted color on all pixels
      for (int i = 0; i < fb->pixelCount; i++) {
        fb->buffer[i] = shiftedColor;
      }
      break;

    case IDLE:
    default:
      // Nothing to draw
      break;
  }

  nextFrame();
}

}  // namespace lamp