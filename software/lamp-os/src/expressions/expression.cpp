#include "expression.hpp"

#include <Arduino.h>

#include <algorithm>

#include "components/network/protocol/lamp_protocol.hpp"
#include "components/transient_override/color_override.hpp"
#include "core/override_aggregate.hpp"
#include "core/behavior_context.hpp"
#include "core/compositor.hpp"
#include "expression_manager.hpp"
#include "expressions/param_utils.hpp"
#include "expressions/primitives.hpp"
#include "util/eased_scalar.hpp"

namespace lamp {

void Expression::configure(const std::vector<Color>& inColors,
                          uint32_t inIntervalMin,
                          uint32_t inIntervalMax,
                          ExpressionTarget inTarget) {
  colors = inColors;
  intervalMinMs = inIntervalMin * kMsPerSecond;
  intervalMaxMs = inIntervalMax * kMsPerSecond;
  target = inTarget;
  scheduleNextTrigger();
}

void Expression::scheduleNextTrigger() {
  nextTriggerMs = millis() + rng.range(intervalMinMs, intervalMaxMs);
}

void Expression::saveBufferState() {
  savedBuffer = fb->buffer;
}

namespace {
// The ColorOverride owning this fb's surface, or nullptr when the buffer
// list isn't wired yet (or fb isn't one of the two surfaces). Shade is
// published first, base second (ExpressionManager::begin()).
ColorOverride* surfaceOverrideFor(BehaviorContext* ctx, FrameBuffer* fb) {
  if (!ctx || ctx->expressionFrameBuffers.size() < 2) return nullptr;
  if (fb == ctx->expressionFrameBuffers[1]) return &lamp::overrides.base;
  if (fb == ctx->expressionFrameBuffers[0]) return &lamp::overrides.shade;
  return nullptr;
}
}  // namespace

bool Expression::shouldAffectBuffer() {
  // Context is wired by Compositor::addBehavior at register time (or by
  // ExpressionManager::setCompositor for transients). Until both are wired
  // and ExpressionManager::begin() has published the buffer list, treat as
  // not-yet-routable and skip.
  if (!context_ || context_->expressionFrameBuffers.size() < 2) return false;

  ColorOverride* ov = surfaceOverrideFor(context_, fb);

  // While the operator has this surface's color editor open, pause all
  // expressions on it so the color being picked reads clean instead of
  // getting overdrawn. Per-surface: editing base leaves shade alone.
  if (ov && ov->operatorEditing()) return false;

  switch (target) {
    case TARGET_SHADE:
      return fb == context_->expressionFrameBuffers[0];
    case TARGET_BASE:
      return fb == context_->expressionFrameBuffers[1];
    case TARGET_BOTH:
      return true;
    default:
      return false;
  }
}

float Expression::wispDimScale() {
  if (!context_ || !context_->compositor ||
      context_->expressionFrameBuffers.size() < 2) {
    return 1.0f;
  }
  const bool isBase = fb == context_->expressionFrameBuffers[1];
  const bool isShade = fb == context_->expressionFrameBuffers[0];
  if (!isBase && !isShade) return 1.0f;
  const float w = context_->compositor->wispPresence(isBase);
  return opacityTarget(true, opacityPct_ / 100.0f, wispDimFloor(), w);
}

void Expression::configureOpacity(const std::map<std::string, uint32_t>& parameters) {
  opacityPct_ = static_cast<uint8_t>(
      std::clamp<uint32_t>(getParam(parameters, "opacity", 100), 10, 100));
}

void Expression::configureEasing(const std::map<std::string, uint32_t>& parameters, uint32_t defVal) {
  easingRaw_ = getParam(parameters, "easing", defVal);
  easing_ = (easingRaw_ == static_cast<uint32_t>(Easing::Random))
                ? randomEasing(rng)
                : static_cast<Easing>(easingRaw_);
}

void Expression::control() {
  // Check for automatic trigger
  if (autoTriggerEnabled && animationState == STOPPED && timeReached(millis(), nextTriggerMs)) {
    trigger();
  }

  // Per-frame updates during animation
  if (animationState == PLAYING || animationState == PLAYING_ONCE) {
    onUpdate();
  }

  // Handle completion when the animation just stopped
  if (animationState == STOPPED && currentLoop > lastCompletedLoop) {
    onComplete();
    lastCompletedLoop = currentLoop;
  }
}

void Expression::continuousControl() {
  if (autoTriggerEnabled && animationState == STOPPED) trigger();
}

bool Expression::previewCycleComplete() {
  if (autoTriggerEnabled) return false;
  return ++previewCyclesDone_ >= kPreviewCycles;
}

Color Expression::getRandomColor() {
  if (colors.empty()) {
    return kSafeFallbackColor;
  }
  return colors[rng.range(0, colors.size() - 1)];
}

Color Expression::firstColorOr(Color fallback) const {
  return colors.empty() ? fallback : colors.front();
}

bool Expression::trigger() {
  // Only trigger if this expression should affect this buffer
  // This ensures expressions respect their target configuration
  if (!shouldAffectBuffer()) {
    return false;
  }

  if (easingRaw_ == static_cast<uint32_t>(Easing::Random)) easing_ = randomEasing(rng);

  // Start immediately
  onTrigger();            // Expression-specific setup
  scheduleNextTrigger();  // Reset next automatic trigger
  playOnce();

  // Notify the manager so the cascade convention fires for all trigger
  // paths, including the per-entry auto-trigger from control().
  // triggerExpression/triggerInvocation set a suppress flag around their
  // own loops so this callback doesn't double-cascade.
  if (context_ && context_->expressionManager) {
    context_->expressionManager->onExpressionFired(this);
  }
  return true;
}

// Lives in lamp.cpp as globals; defined in lamp namespace.
}  // namespace lamp