#include "expression.hpp"

#include <Arduino.h>

#include "components/network/lamp_protocol.hpp"
#include "components/transient_override/color_override.hpp"
#include "core/override_aggregate.hpp"
#include "core/behavior_context.hpp"
#include "core/compositor.hpp"
#include "expression_manager.hpp"

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
  nextTriggerMs = millis() + rng.range(intervalMinMs, intervalMaxMs);
}

void Expression::saveBufferState() {
  savedBuffer = fb->buffer;
}

// Defined out-of-line at the bottom of this file. Queries the lamp's
// two ColorOverride globals — returns true iff the wisp is actively
// holding either surface. Used by shouldAffectBuffer() AND control()
// below to suppress disabled-during-wisp expressions.
bool isWispCurrentlyOverriding();

bool Expression::shouldAffectBuffer() {
  // Context is wired by Compositor::addBehavior at register time (or by
  // ExpressionManager::setCompositor for transients). Until both are wired
  // and ExpressionManager::begin() has published the buffer list, treat as
  // not-yet-routable and skip — same behavior as the old empty-vector guard.
  if (!context_ || context_->expressionFrameBuffers.size() < 2) return false;

  // Check if current buffer matches our target
  bool isShade = (fb == context_->expressionFrameBuffers[0]);  // Shade is first
  bool isBase = (fb == context_->expressionFrameBuffers[1]);   // Base is second

  // Wisp-override draw gate. control() already suppresses auto-trigger
  // and (for Breathing) per-frame onUpdate while wisp paint is held, but
  // an expression that was already PLAYING when wisp activated keeps
  // its CACHED last frame — and draw() runs each tick regardless,
  // stomping the wisp paint with stale data. The 2026-06-13 "jacko
  // renders pink despite correct wisp blue at beginFade" symptom was
  // exactly this: a frozen BreathingExpression::draw() repainting its
  // last targetColor over the wisp gradient every frame. Suppress
  // draw too while wisp owns the relevant surface.
  if (disabledDuringWispOverride() && isWispCurrentlyOverriding()) {
    return false;
  }

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
  // Wisp-override gate: skip auto-trigger when the operator has marked
  // this expression "pause when wisp is in control" AND a wisp paint
  // is currently held on either surface. Pushes nextTriggerMs forward
  // by the min interval so a long-running wisp hold doesn't queue up
  // a backlog of triggers that all fire the instant the wisp lets go.
  if (disabledDuringWispOverride() && animationState == STOPPED &&
      isWispCurrentlyOverriding()) {
    nextTriggerMs = millis() + intervalMinMs;
    return;
  }

  // Check for automatic trigger
  if (autoTriggerEnabled && animationState == STOPPED && millis() > nextTriggerMs) {
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

Color Expression::getRandomColor() {
  if (colors.empty()) {
    return kSafeFallbackColor;
  }
  return colors[rng.range(0, colors.size() - 1)];
}

Color Expression::firstColorOr(Color fallback) const {
  return colors.empty() ? fallback : colors.front();
}

void Expression::trigger() {
  // Only trigger if this expression should affect this buffer
  // This ensures expressions respect their target configuration
  if (!shouldAffectBuffer()) {
    return;
  }

  // Start immediately
  onTrigger();            // Expression-specific setup
  scheduleNextTrigger();  // Reset next automatic trigger
  playOnce();

  // Notify the manager so the cascade convention fires for ALL trigger paths,
  // including the per-entry auto-trigger from control() (which previously
  // bypassed maybeCascade entirely). triggerExpression/triggerInvocation set
  // a suppress flag on the manager around their own loops so this callback
  // doesn't double-cascade (or re-cascade for remote-arrived invocations).
  if (context_ && context_->expressionManager) {
    context_->expressionManager->onExpressionFired(this);
  }
}

// Lives in lamp.cpp as globals; defined in lamp namespace.
}  // namespace lamp

namespace lamp {

// Forward-declared in this TU above Expression::control(). Cheap query
// — two bool reads + two enum comparisons per Expression per loop
// tick. The override aggregate lives in override_aggregate.cpp.
bool isWispCurrentlyOverriding() {
  if (lamp::overrides.base.isActive() &&
      lamp::overrides.base.activeSource() ==
          lamp_protocol::OverrideSource::Wisp) {
    return true;
  }
  if (lamp::overrides.shade.isActive() &&
      lamp::overrides.shade.activeSource() ==
          lamp_protocol::OverrideSource::Wisp) {
    return true;
  }
  return false;
}

}  // namespace lamp