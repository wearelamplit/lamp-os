#include "expressions/shifty/shifty_expression.hpp"

#include <Arduino.h>

#include "util/fade.hpp"
#include "expressions/expression_manager.hpp"

namespace lamp {

namespace {
constexpr ExpressionDescriptor kShiftyDescriptor =
    withMake(kShiftyDescriptorData, &makeExpr<ShiftyExpression>);
}  // namespace

ShiftyExpression::ShiftyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
}

const ExpressionDescriptor& ShiftyExpression::classDescriptor() {
  return kShiftyDescriptor;
}

const ExpressionDescriptor& ShiftyExpression::descriptor() const {
  return kShiftyDescriptor;
}

void ShiftyExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  uint32_t shiftDurationMin = getParam(parameters, "shiftDurationMin");
  uint32_t shiftDurationMax = getParam(parameters, "shiftDurationMax");
  uint32_t fadeDuration = getParam(parameters, "fadeDuration");

  shiftDurationMinMs = shiftDurationMin * kMsPerSecond;
  shiftDurationMaxMs = shiftDurationMax * kMsPerSecond;
  fadeDurationMs = fadeDuration * kMsPerSecond;

  const uint16_t window = windowSize();
  zone_ = resolveZone(parameters, window);
  uint32_t fmv = getParam(parameters, "fillMode");
  fillMode_ = (fmv > 3) ? 3 : static_cast<uint8_t>(fmv);
  easing_ = static_cast<Easing>(getParam(parameters, "easing", 0));

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

  // The whole shift cycle (fade + hold + fade, up to 2400 s) outruns any
  // frame budget; onUpdate ends each phase on millis() and concludes the
  // cycle by capping frames, so the counter never ends a phase itself.
  frames = kContinuousMaxFrames;
  frame = 0;
  fadeStartMs = millis();
  state = FADING_TO_PALETTE;
  if (fillMode_ != 0) populatePixelStartOffsets(false);

  // Determine how long to stay shifted
  currentShiftDurationMs = getRandomShiftDuration();
  shiftStartMs = millis();
#ifdef LAMP_DEBUG
  Serial.printf("[shifty] fade-start t=%lu fadeMs=%u holdMs=%u\n",
                (unsigned long)fadeStartMs, (unsigned)fadeDurationMs,
                (unsigned)currentShiftDurationMs);
#endif
}

void ShiftyExpression::startUnshift() {
  // Set up fade back to original
  // Start from the shifted color (what is currently showing)
  fadeStartColors.assign(fb->pixelCount, shiftedColor);
  fadeTargetColors = savedBuffer;

  fadeStartMs = millis();
  state = FADING_BACK;
#ifdef LAMP_DEBUG
  Serial.printf("[shifty] fade-back-start t=%lu\n", (unsigned long)fadeStartMs);
#endif
  if (fillMode_ != 0) populatePixelStartOffsets(true);

  // Keep animation running (don't change animationState, it's already PLAYING_ONCE)
}

void ShiftyExpression::populatePixelStartOffsets(bool fadingBack) {
  const uint16_t n = zone_.size();
  pixelStartOffsetMs_.assign(static_cast<size_t>(fb->pixelCount), 0u);
  if (n == 0 || fadeDurationMs < 2) return;
  const uint32_t maxOffset = fadeDurationMs / 2;
  const uint32_t denom = (n > 1u) ? static_cast<uint32_t>(n - 1) : 1u;
  for (uint16_t j = 0; j < n; j++) {
    const int idx = static_cast<int>(zone_.posMin) + static_cast<int>(j);
    if (idx >= fb->pixelCount) break;
    uint32_t ord;
    switch (fillMode_) {
      case 1: ord = j; break;
      case 2: ord = denom - j; break;
      default: {  // 3: middle-out forward, outside-in on fade-back
        const int d = 2 * static_cast<int>(j) - static_cast<int>(n - 1);
        const uint32_t dist = static_cast<uint32_t>(d >= 0 ? d : -d);
        ord = fadingBack ? (denom - dist) : dist;
        break;
      }
    }
    pixelStartOffsetMs_[static_cast<size_t>(idx)] = ord * maxOffset / denom;
  }
}

uint32_t ShiftyExpression::getRandomShiftDuration() {
  return rng.range(shiftDurationMinMs, shiftDurationMaxMs);
}

void ShiftyExpression::onTrigger() {
  saveBufferState();

  // If already animating, cancel and start fresh
  // This allows manual triggers to work at any time
  if (state != IDLE) {
    // Reset to idle state
    state = IDLE;
  }

  startShift();
}

void ShiftyExpression::onUpdate() {
  const uint32_t nowMs = millis();

  switch (state) {
    case FADING_TO_PALETTE:
      if (nowMs - fadeStartMs >= fadeDurationMs) {
        state = SHIFTED;
        shiftStartMs = nowMs;
#ifdef LAMP_DEBUG
        Serial.printf("[shifty] hold-start t=%lu\n", (unsigned long)nowMs);
#endif
      }
      break;

    case SHIFTED:
      // Check if it's time to unshift
      if (nowMs - shiftStartMs > currentShiftDurationMs) {
        startUnshift();
      }
      break;

    case FADING_BACK:
      if (nowMs - fadeStartMs >= fadeDurationMs) {
        state = IDLE;
#ifdef LAMP_DEBUG
        Serial.printf("[shifty] complete t=%lu\n", (unsigned long)nowMs);
#endif
        // Cap the counter; the next nextFrame() flips PLAYING_ONCE to
        // STOPPED and onComplete chains glitchy.
        frames = frame + 1;
      }
      break;

    default:
      break;
  }

  // Perf: precompute the per-frame fade factor here so draw() can apply it
  // per pixel without redoing the LUT-equivalent math for every channel.
  // Only meaningful in FADING_* states; SHIFTED and IDLE branches in draw()
  // don't consult the factor.
  if (state == FADING_TO_PALETTE || state == FADING_BACK) {
    const uint32_t elapsed = nowMs - fadeStartMs;
    cachedFadeAtEnd_ = (elapsed >= fadeDurationMs);
    // When cachedFadeAtEnd_ is true, draw() short-circuits to the end color
    // and never reads cachedFadeFactor_, so a divide-by-zero in
    // computeLinearFactor (when fadeDurationMs == 0) is unreachable here.
    cachedFadeFactor_ = cachedFadeAtEnd_
                            ? 0u
                            : computeLinearFactor(easeStep(elapsed, fadeDurationMs, easing_),
                                                  fadeDurationMs);
  }
}

void ShiftyExpression::onComplete() {
  // Always trigger glitch on unshift if glitchy is available and the fade back just finished
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
  // Mid-cycle (including a wisp-suppressed one) the counter must never
  // exhaust; a wrap would flip STOPPED and snap the strip to base with no
  // fade-back. onUpdate() ends the cycle on wall clock.
  if (state != IDLE) frame = rewindBeforeExhaust(frame, frames);

  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  switch (state) {
    case FADING_TO_PALETTE:
    case FADING_BACK: {
      if (fillMode_ == 0) {
        if (cachedFadeAtEnd_) {
          for (int i = zone_.posMin; i <= zone_.posMax; i++) {
            if (i >= fb->pixelCount) break;
            fb->buffer[i] = fadeTargetColors[i];
          }
        } else {
          for (int i = zone_.posMin; i <= zone_.posMax; i++) {
            if (i >= fb->pixelCount) break;
            fb->buffer[i] = mixColorLinear(fadeStartColors[i], fadeTargetColors[i],
                                            cachedFadeFactor_);
          }
        }
      } else {
        const uint32_t elapsed = millis() - fadeStartMs;
        const uint32_t perFade = (fadeDurationMs > 1u) ? fadeDurationMs / 2u : 1u;
        for (int i = zone_.posMin; i <= zone_.posMax; i++) {
          if (i >= fb->pixelCount) break;
          const uint32_t offset = pixelStartOffsetMs_[static_cast<size_t>(i)];
          if (elapsed < offset) {
            fb->buffer[i] = fadeStartColors[i];
          } else {
            const uint32_t pixelElapsed = elapsed - offset;
            if (pixelElapsed >= perFade) {
              fb->buffer[i] = fadeTargetColors[i];
            } else {
              fb->buffer[i] = mixColorLinear(
                  fadeStartColors[i], fadeTargetColors[i],
                  computeLinearFactor(easeStep(pixelElapsed, perFade, easing_), perFade));
            }
          }
        }
      }
      break;
    }

    case SHIFTED:
      // Re-snapshot the live base (configurator drew it this frame, before
      // shifty) so startUnshift fades back to the CURRENT base, not the one
      // captured at onTrigger; an operator base change mid-hold otherwise
      // pops in one frame when shifty releases.
      savedBuffer = fb->buffer;
      if (fillMode_ == 0) {
        for (int i = zone_.posMin; i <= zone_.posMax; i++) {
          if (i >= fb->pixelCount) break;
          fb->buffer[i] = shiftedColor;
        }
      } else {
        for (int i = zone_.posMin; i <= zone_.posMax; i++) {
          if (i >= fb->pixelCount) break;
          fb->buffer[i] = shiftedColor;
        }
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
