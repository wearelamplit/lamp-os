#include "expressions/shifty/shifty_expression.hpp"

#include <Arduino.h>
#include <algorithm>

#include "util/fade.hpp"
#include "expressions/expression_manager.hpp"

namespace lamp {

namespace {
static constexpr EnumOption kShiftyFillOpts[] = {
  { .value = 0, .label = "Uniform", .zoning = false },
  { .value = 1, .label = "Up",      .zoning = true  },
  { .value = 2, .label = "Down",    .zoning = true  },
  { .value = 3, .label = "Bloom",   .zoning = true  },
};
static constexpr ParamSpec kShiftyParams[] = {
  {
    .key     = "fillMode",
    .kind    = ParamKind::Enum,
    .label   = "Fill",
    .max     = 3,
    .options = kShiftyFillOpts,
  },
  {
    .key        = "fadeDuration",
    .kind       = ParamKind::Int,
    .label      = "Fade duration",
    .min        = 10,
    .max        = 300,
    .def        = 60,
    .unit       = "s",
    .leftLabel  = "quick",
    .rightLabel = "slow",
  },
};
// duration models the hold time (shiftDurationMin/Max in seconds).
static constexpr ExpressionDescriptor kShiftyDescriptor{
  .id           = "shifty",
  .name         = "Shifty",
  .continuous   = true,
  .pausesWispOverride = true,
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
    .min    = 60,
    .max    = 1800,
    .step   = 30,
    .unit   = "s",
    .defLo  = 300,
    .defHi  = 600,
    .label  = "Hold time",
    .minKey = "shiftDurationMin",
    .maxKey = "shiftDurationMax",
  },
  .hasZone      = true,
  .params       = kShiftyParams,
  .make         = &makeExpr<ShiftyExpression>,
};
}  // namespace

ShiftyExpression::ShiftyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
  allowedInHomeMode = true;  // Shifty should work in home mode
}

const ExpressionDescriptor& ShiftyExpression::descriptor() {
  return kShiftyDescriptor;
}

void ShiftyExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  uint32_t shiftDurationMin = getParam(parameters, "shiftDurationMin");
  uint32_t shiftDurationMax = getParam(parameters, "shiftDurationMax");
  uint32_t fadeDuration = getParam(parameters, "fadeDuration");

  shiftDurationMinMs = shiftDurationMin * kMsPerSecond;
  shiftDurationMaxMs = shiftDurationMax * kMsPerSecond;
  fadeDurationFrames = fadeDuration * kFrameRateHz;

  const uint16_t window = windowSize();
  zone_ = Zone::fromParameters(parameters, window);
  uint32_t fmv = getParam(parameters, "fillMode");
  fillMode_ = (fmv > 3) ? 3 : static_cast<uint8_t>(fmv);

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
  if (fillMode_ != 0) populatePixelStartFrames(false);

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
  if (fillMode_ != 0) populatePixelStartFrames(true);

  // Keep animation running (don't change animationState, it's already PLAYING_ONCE)
}

void ShiftyExpression::populatePixelStartFrames(bool fadingBack) {
  const uint16_t n = zone_.size();
  pixelStartFrame_.assign(static_cast<size_t>(fb->pixelCount), 0u);
  if (n == 0 || fadeDurationFrames < 2) return;
  const uint32_t maxOffset = fadeDurationFrames / 2;
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
    pixelStartFrame_[static_cast<size_t>(idx)] = ord * maxOffset / denom;
  }
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
        frames = frame + (currentShiftDurationMs / static_cast<float>(kMsPerSecond)) * kFrameRateHz;
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
      if (fillMode_ == 0) {
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
      } else {
        const uint32_t perFade = (fadeDurationFrames > 1u) ? fadeDurationFrames / 2u : 1u;
        for (int i = zone_.posMin; i <= zone_.posMax; i++) {
          if (i >= fb->pixelCount) break;
          const uint32_t offset = pixelStartFrame_[static_cast<size_t>(i)];
          if (frame < offset) {
            fb->buffer[i] = fadeStartColors[i];
          } else {
            const uint32_t elapsed = frame - offset;
            if (elapsed >= perFade) {
              fb->buffer[i] = fadeTargetColors[i];
            } else {
              fb->buffer[i] = mixColorLinear(fadeStartColors[i], fadeTargetColors[i],
                                              computeLinearFactor(elapsed, perFade));
            }
          }
        }
      }
      break;
    }

    case SHIFTED:
      // Re-snapshot the live base (configurator drew it this frame, before
      // shifty) so startUnshift fades back to the CURRENT base, not the one
      // captured at onTrigger — an operator base change mid-hold otherwise
      // pops in one frame when shifty releases.
      savedBuffer = fb->buffer;
      if (fillMode_ == 0) {
        for (int i = 0; i < fb->pixelCount; i++) {
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