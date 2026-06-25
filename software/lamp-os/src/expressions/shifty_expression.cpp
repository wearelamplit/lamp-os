#include "shifty_expression.hpp"

#include <Arduino.h>
#include <algorithm>

#include "util/fade.hpp"
#include "expression_manager.hpp"

namespace lamp {

ShiftyExpression::ShiftyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
  allowedInHomeMode = true;  // Shifty should work in home mode
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
  // Pick a random color to shift to. getRandomColor() returns
  // Expression::kSafeFallbackColor when the palette is empty, so no explicit
  // fallback branch needed here.
  shiftedColor = getRandomColor();

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
  return rng.range(shiftDurationMinMs, shiftDurationMaxMs);
}

void ShiftyExpression::onTrigger() {
  saveBufferState();

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

  // Perf: precompute the per-frame fade factor here so draw() can apply it
  // per pixel without redoing the LUT-equivalent math for every channel.
  // (frame, frames) are frame-scoped; per-pixel work in draw() only reads
  // fadeStartColors[i] / fadeTargetColors[i]. Only meaningful in FADING_*
  // states; SHIFTED and IDLE branches in draw() don't consult the factor.
  if (state == FADING_TO_PALETTE || state == FADING_BACK) {
    cachedFadeAtEnd_ = (frame >= frames);
    // When cachedFadeAtEnd_ is true, draw() short-circuits to the end color
    // and never reads cachedFadeFactor_, so a divide-by-zero in
    // computeLinearFactor (when frames == 0) is unreachable here.
    cachedFadeFactor_ = cachedFadeAtEnd_ ? 0u : computeLinearFactor(frame, frames);
  }
}

void ShiftyExpression::onComplete() {
  // Always trigger glitch on unshift if glitchy is available and we just finished fading back
  if (state == IDLE) {
    if (context_ && context_->expressionManager) {
#ifdef LAMP_DEBUG
      Serial.println("[shifty] onComplete IDLE → triggerExpression(glitchy)");
#endif
      context_->expressionManager->triggerExpression("glitchy");
    } else {
#ifdef LAMP_DEBUG
      Serial.printf("[shifty] onComplete IDLE but no chain (context=%d mgr=%d)\n",
                    context_ != nullptr,
                    (context_ && context_->expressionManager) ? 1 : 0);
#endif
    }
  }
}

void ShiftyExpression::draw() {
  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  switch (state) {
    case FADING_TO_PALETTE:
    case FADING_BACK: {
      // Per-frame fade factor was cached in onUpdate(); apply per pixel here.
      // End-clamp short-circuit mirrors easeLinear()'s `currentStep >= duration`
      // branch which returns `end` regardless of start.
      if (cachedFadeAtEnd_) {
        for (int i = 0; i < fb->pixelCount; i++) {
          fb->buffer[i] = fadeTargetColors[i];
        }
      } else {
        for (int i = 0; i < fb->pixelCount; i++) {
          fb->buffer[i] = mixColorLinear(fadeStartColors[i], fadeTargetColors[i],
                                          cachedFadeFactor_);
        }
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