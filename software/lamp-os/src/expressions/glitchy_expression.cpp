#include "glitchy_expression.hpp"
#include <Arduino.h>
#include "util/fade.hpp"

namespace lamp {

namespace {
// Glitchy always calls fadeLinear(buffer[i], glitchColor, 100, 95): the
// factor is a constant. Precompute it once. Mirrors easeLinear's:
//   linear[(uint16_t)((95 * 511 / 100 * 511) / 511)] = linear[485] = 485 * 511
// (linear[i] == i * 511 by construction; see src/util/fade.cpp).
static constexpr uint32_t kGlitchyLinearFactor = 485u * 511u;  // == 247835
}  // namespace

GlitchyExpression::GlitchyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
  allowedInHomeMode = false;  // Glitchy should not work in home mode
}

void GlitchyExpression::configureFromParameters(const std::map<std::string, uint32_t>& parameters) {
  // Extract duration range parameters with default values
  auto itMin = parameters.find("durationMin");
  uint32_t durationMin = (itMin != parameters.end()) ? itMin->second : 1;

  auto itMax = parameters.find("durationMax");
  uint32_t durationMax = (itMax != parameters.end()) ? itMax->second : 3;

  // Validate and set duration range
  glitchDurationMin = durationMin > 0 && durationMin <= 60 ? durationMin : 1;
  glitchDurationMax = durationMax > 0 && durationMax <= 60 ? durationMax : 3;

  // Ensure max is at least as large as min
  if (glitchDurationMax < glitchDurationMin) {
    glitchDurationMax = glitchDurationMin;
  }
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

  // On last frame, restore original buffer
  if (isLastFrame()) {
    fb->buffer = savedBuffer;
  } else {
    // Blend glitch color with current buffer for a tinted effect.
    // 95% blend = 95 out of 100 steps toward glitch color. Inputs (100, 95)
    // are constant, so the linear factor is too — precomputed as
    // kGlitchyLinearFactor at namespace scope. Per pixel we just inline the
    // four-channel mix and skip the per-channel function-call overhead.
    for (int i = 0; i < fb->pixelCount; i++) {
      fb->buffer[i] = mixColorLinear(fb->buffer[i], glitchColor, kGlitchyLinearFactor);
    }
  }

  nextFrame();
}

void GlitchyExpression::onComplete() {
  // ~33% chance to chain a second glitch 200–600 ms after this one
  // finishes, ignoring the configured intervalMin/Max. The base
  // class's scheduleNextTrigger() handles the next regular trigger
  // when this short-circuit doesn't fire.
  if (rng.range(0, 99) < 33) {
    nextTriggerMs = millis() + rng.range(200, 600);
  }
}

}  // namespace lamp