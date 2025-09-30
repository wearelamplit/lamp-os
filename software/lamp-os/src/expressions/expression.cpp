#include "./expression.hpp"

#include <Arduino.h>

#include "../core/compositor.hpp"
#include "./expression_manager.hpp"

// Global frame buffer references (set by ExpressionManager)
namespace lamp {
  extern std::vector<FrameBuffer*> expressionFrameBuffers;

  // Global compositor pointer (set by standard_lamp)
  static Compositor* globalCompositor = nullptr;

  void setGlobalCompositor(Compositor* compositor) {
    globalCompositor = compositor;
  }
}

namespace lamp {

void Expression::configure(const std::vector<Color>& inColors,
                          uint32_t inIntervalMin,
                          uint32_t inIntervalMax,
                          ExpressionTarget inTarget) {
  colors = inColors;
  intervalMinMs = inIntervalMin * 1000;
  intervalMaxMs = inIntervalMax * 1000;
  target = inTarget;
  scheduleNextTrigger();
}

void Expression::scheduleNextTrigger() {
  std::uniform_int_distribution<uint32_t> dist(intervalMinMs, intervalMaxMs);
  nextTriggerMs = millis() + dist(rng);
}

void Expression::saveBufferState() {
  savedBuffer = fb->buffer;
}

bool Expression::shouldAffectBuffer() {
  if (expressionFrameBuffers.empty()) return false;

  // Check if current buffer matches our target
  bool isShade = (fb == expressionFrameBuffers[0]);  // Shade is first
  bool isBase = (fb == expressionFrameBuffers[1]);   // Base is second

  switch (target) {
    case TARGET_SHADE:
      return isShade;
    case TARGET_BASE:
      return isBase;
    case TARGET_BOTH:
      return true;
    default:
      return false;
  }
}

void Expression::control() {
  // Pause if an exclusive behavior is running (unless we are exclusive)
  if (shouldPause()) return;


  // Check for automatic trigger
  if (animationState == STOPPED && millis() > nextTriggerMs) {
    trigger();
  }

  // Per-frame updates during animation
  if (animationState == PLAYING || animationState == PLAYING_ONCE) {
    onUpdate();
  }

  // Handle completion - check if we just stopped
  if (animationState == STOPPED && currentLoop > lastCompletedLoop) {
    onComplete();
    lastCompletedLoop = currentLoop;
  }
}

bool Expression::shouldPause() const {
  // Don't pause if this expression is exclusive
  if (isExclusive) return false;

  // Check if compositor has an active exclusive
  return globalCompositor && globalCompositor->hasActiveExclusive();
}

Color Expression::getRandomColor() {
  if (colors.empty()) {
    return Color(0, 0, 0, 0);
  }
  std::uniform_int_distribution<size_t> dist(0, colors.size() - 1);
  return colors[dist(rng)];
}

void Expression::trigger() {

  // Only trigger if this expression should affect this buffer
  // This ensures expressions respect their target configuration
  if (!shouldAffectBuffer()) {
    return;
  }


  // Save current state and start immediately
  saveBufferState();
  onTrigger();            // Expression-specific setup
  scheduleNextTrigger();  // Reset next automatic trigger
  playOnce();
}

uint32_t Expression::extractUint32Parameter(const std::map<std::string, uint32_t>& parameters,
                                             const std::string& key, uint32_t defaultValue) const {
  auto it = parameters.find(key);
  if (it != parameters.end()) {
    return it->second;
  }
  return defaultValue;
}

}  // namespace lamp