#include "expressions/breathing/breathing_expression.hpp"

#include <Arduino.h>
#include <algorithm>
#include <cmath>

#include "util/fade.hpp"

namespace lamp {

namespace {
static constexpr ParamSpec kBreathingParams[] = {
  {
    .key        = "breathSpeed",
    .kind       = ParamKind::Int,
    .label      = "Breath cycle length",
    .min        = 1,
    .max        = 60,
    .def        = 10,
    .unit       = "s",
    .invert     = true,
    .leftLabel  = "slow",
    .rightLabel = "fast",
  },
  {
    .key   = "count",
    .kind  = ParamKind::Int,
    .label = "Points",
    .min   = 1,
    .max   = Bound::pixels(10),
    .def   = 1,
  },
  {
    .key   = "size",
    .kind  = ParamKind::Int,
    .label = "Size",
    .min   = 1,
    .max   = Bound::pixels(),
    .def   = Bound::pixels(),
  },
  {
    .key        = "scatter",
    .kind       = ParamKind::Int,
    .label      = "Spread",
    .max        = 100,
    .unit       = "%",
    .leftLabel  = "Together",
    .rightLabel = "Scattered",
  },
};
static constexpr ExpressionDescriptor kBreathingDescriptor{
  .id         = "breathing",
  .name       = "Breathing",
  .continuous = true,
  .pausesWispOverride = true,
  .colors     = { .max = 8, .label = "Colors" },
  .hasZone    = true,
  .params     = kBreathingParams,
  .make       = &makeExpr<BreathingExpression>,
};
}  // namespace

// Large frame count to keep animation running continuously
static constexpr uint32_t BREATHING_MAX_FRAMES = 100000;

const ExpressionDescriptor& BreathingExpression::descriptor() {
  return kBreathingDescriptor;
}

BreathingExpression::BreathingExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
  allowedInHomeMode = true;
}

void BreathingExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  uint32_t breathSpeed = getParam(parameters, "breathSpeed");
  if (breathSpeed < 1) breathSpeed = 1;
  breathSpeedMs = breathSpeed * kMsPerSecond;

  targetColor = firstColorOr(kSafeFallbackColor);

  const uint16_t window = windowSize();
  zone_ = Zone::fromParameters(parameters, window);
  points_ = Points::fromParameters(parameters, window, 1).count;
  size_ = parseSize(parameters, window, window);
  scatter_ = Scatter::fromParameters(parameters).percent;
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

    // Transient one-shot path: when autoTriggerEnabled is false, this
    // expression was created by ExpressionManager::triggerInvocation as a
    // remote-cascaded one-breath instance (see expression_manager.cpp:242).
    // BreathingExpression normally runs continuously with frames=100000 and
    // animationState=PLAYING, which never naturally reaches STOPPED — so
    // gcTransients() would never reap it (memory leak). Mark one complete
    // cycle here so isAnimationComplete() returns true on the next tick.
    if (!autoTriggerEnabled) {
      animationState = STOPPED;
      lastCompletedLoop = currentLoop + 1;
      lastBreathUpdateMs = currentMs;
      return;
    }

    // Advance to next color if we have multiple colors
    if (colors.size() > 1) {
      if (cyclingForward) {
        currentColorIndex++;
        // If we reached the last color, switch to backward. Written as
        // `currentColorIndex + 1 >= colors.size()` (not `>= size - 1`) so the
        // comparison is safe if the upstream `colors.size() > 1` guard ever
        // moves — unsigned subtraction would underflow on an empty palette.
        if (currentColorIndex + 1 >= colors.size()) {
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
  breathPhase = 0.0f;
  lastBreathUpdateMs = 0;

  currentColorIndex = 0;
  cyclingForward = true;

  frames = BREATHING_MAX_FRAMES;
  frame = 0;

  pointPos_.clear();
  pointPhase_.clear();
  const uint16_t regionSize = zone_.size();
  if (fb && fb->pixelCount > 0 && regionSize > 0 && points_ > 0) {
    const uint16_t clampedSize = std::min(size_, regionSize);
    const uint16_t range = regionSize - clampedSize;
    const float phaseSpread = scatter_ / 100.0f;
    pointPos_.reserve(points_);
    pointPhase_.reserve(points_);
    for (uint16_t p = 0; p < points_; ++p) {
      uint16_t start = zone_.posMin;
      if (points_ > 1 && range > 0) {
        start = static_cast<uint16_t>(zone_.posMin +
                (static_cast<uint32_t>(p) * range) / (points_ - 1));
      }
      pointPos_.push_back(start);
      pointPhase_.push_back(phaseSpread * static_cast<float>(p) / static_cast<float>(points_));
    }
  }

  play();
}

void BreathingExpression::onUpdate() {
  // Always update breath phase
  updateBreathPhase();

  // Perf: precompute the per-frame fade factor here so draw() can apply it
  // per pixel without recomputing the LUT-equivalent math for every channel.
  // Inputs (breathPhase, breathSpeedMs) are frame-scoped, not pixel-scoped.
  float intensity = 0.5f - 0.5f * cosf(breathPhase * 2.0f * static_cast<float>(M_PI));
  uint32_t breathIntensity = static_cast<uint32_t>(intensity * 100.0f);
  // Mirror easeLinear's end-clamp short-circuit (currentStep >= duration).
  cachedFadeAtEnd_ = (breathIntensity >= 100u);
  cachedFadeFactor_ = computeLinearFactor(breathIntensity, 100u);
}

void BreathingExpression::control() {
  // Base-class gate runs only at trigger time; continuous expressions check here. See docs/dev/expressions.md.
  if (disabledDuringWispOverride() && isWispCurrentlyOverriding()) {
    // Reset so the first tick after the wisp releases recomputes phase from the current time.
    // A multi-cycle hold would otherwise add one huge phaseIncrement and skip palette colors on resume.
    lastBreathUpdateMs = 0;
    return;
  }

  // Transient instances (autoTriggerEnabled=false) must not retrigger; gcTransients() needs them STOPPED to reap.
  if (autoTriggerEnabled && animationState == STOPPED) {
    trigger();
  }

  if (animationState == PLAYING || animationState == PLAYING_ONCE) {
    onUpdate();
  }
}

void BreathingExpression::draw() {
  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  const bool identity = (scatter_ == 0 &&
                         zone_.posMin == 0 &&
                         zone_.posMax == static_cast<uint16_t>(fb->pixelCount - 1) &&
                         points_ == 1 &&
                         size_ >= static_cast<uint16_t>(fb->pixelCount));
  if (identity) {
    if (cachedFadeAtEnd_) {
      for (int i = 0; i < fb->pixelCount; i++) {
        fb->buffer[i] = targetColor;
      }
    } else {
      for (int i = 0; i < fb->pixelCount; i++) {
        fb->buffer[i] = mixColorLinear(fb->buffer[i], targetColor, cachedFadeFactor_);
      }
    }
  } else {
    const uint16_t regionSize = zone_.size();
    const uint16_t clampedSize = (regionSize > 0) ? std::min(size_, regionSize) : 0;
    for (size_t p = 0; p < pointPos_.size(); ++p) {
      float phase = breathPhase + pointPhase_[p];
      if (phase >= 1.0f) phase -= 1.0f;
      const float intensity = 0.5f - 0.5f * cosf(phase * 2.0f * static_cast<float>(M_PI));
      const uint32_t pIntensity = static_cast<uint32_t>(intensity * 100.0f);
      const int start = static_cast<int>(pointPos_[p]);
      const int end = std::min(start + static_cast<int>(clampedSize), static_cast<int>(fb->pixelCount));
      for (int i = start; i < end; ++i) {
        if (pIntensity >= 100u) {
          fb->buffer[i] = targetColor;
        } else {
          fb->buffer[i] = mixColorLinear(fb->buffer[i], targetColor,
                                         computeLinearFactor(pIntensity, 100u));
        }
      }
    }
  }

  nextFrame();
}

}  // namespace lamp
