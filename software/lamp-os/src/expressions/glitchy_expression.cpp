#include "./glitchy_expression.hpp"

#include "../util/fade.hpp"

namespace lamp {

GlitchyExpression::GlitchyExpression(FrameBuffer* inBuffer, uint32_t inFrames)
    : Expression(inBuffer, inFrames) {
  isExclusive = true;  // Glitchy takes exclusive control when active
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
  glitchColor = getRandomColor();

  // Randomly pick duration between min and max
  if (glitchDurationMin == glitchDurationMax) {
    frames = glitchDurationMin;
  } else {
    std::uniform_int_distribution<uint32_t> durationDist(glitchDurationMin, glitchDurationMax);
    frames = durationDist(rng);
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
    // Blend glitch color with current buffer for a tinted effect
    // 95% blend = 95 out of 100 steps toward glitch color
    for (int i = 0; i < fb->pixelCount; i++) {
      fb->buffer[i] = fadeLinear(fb->buffer[i], glitchColor, 100, 95);
    }
  }

  nextFrame();

  // Check if animation just completed
  if (animationState == STOPPED && frame >= frames) {
  }
}

}  // namespace lamp