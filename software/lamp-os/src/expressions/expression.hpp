#pragma once

#include <cstdint>
#include <map>
#include <variant>
#include <vector>

#include "core/animated_behavior.hpp"
#include "expressions/expression_schema.hpp"
#include "util/color.hpp"
#include "util/fast_rng.hpp"

namespace lamp {

// True iff the wisp is currently holding an override on either the
// base or shade surface. Defined in expression.cpp. Used by
// Expression::control() and any continuous subclass that overrides
// control() to honour `disabledDuringWispOverride`. See
// `docs/dev/expressions.md` for the semantics.
bool isWispCurrentlyOverriding();

enum ExpressionTarget {
  TARGET_SHADE = 1,
  TARGET_BASE = 2,
  TARGET_BOTH = 3
};

/**
 * Base class for lamp expressions - behaviors that add personality.
 * Expressions are time-triggered behaviors that modify the lamp's appearance.
 */
class Expression : public AnimatedBehavior {
 protected:
  std::vector<Color> savedBuffer;
  std::vector<Color> colors;
  uint32_t nextTriggerMs = 0;
  uint32_t intervalMinMs = 60000;   // 1 min default
  uint32_t intervalMaxMs = 900000;  // 15 min default
  uint32_t lastCompletedLoop = 0;   // Track last completed animation loop
  ExpressionTarget target = TARGET_BOTH;
  FastRng rng;

  /**
   * Schedule next trigger within configured interval range.
   */
  void scheduleNextTrigger();

  /**
   * Save current buffer state for restoration.
   */
  void saveBufferState();

  /**
   * Check if this expression should affect current buffer.
   */
  bool shouldAffectBuffer();

 public:
  using AnimatedBehavior::AnimatedBehavior;

  virtual ~Expression() = default;

  /**
   * Configure expression parameters (initial setup).
   * @param inColors Color palette for the expression
   * @param inIntervalMin Minimum trigger interval in seconds
   * @param inIntervalMax Maximum trigger interval in seconds
   * @param inTarget Which lamp component to affect
   */
  void configure(const std::vector<Color>& inColors,
                 uint32_t inIntervalMin,
                 uint32_t inIntervalMax,
                 ExpressionTarget inTarget);

  // Returns the static descriptor for this expression type.
  virtual const ExpressionDescriptor& descriptor() const = 0;

  // Surfaces the expression id string for the compositor home-mode gate.
  const char* homeModeExpressionId() const override { return descriptor().id; }

  // Apply expression-specific tuning params keyed as in descriptor().params.
  virtual void configureFromParameters(
      const std::map<std::string, uint32_t>& parameters) = 0;

  void control() override;

  /**
   * Manually trigger this expression to start immediately.
   * Can be called from UI or other expressions.
   * @return true if the animation started; false when gated off (wrong
   *         buffer, or a wisp hold on a disabledDuringWispOverride type)
   */
  bool trigger();

  /**
   * Get random color from configured palette. Returns
   * Expression::kSafeFallbackColor when the palette is empty so all
   * four expression subclasses share one well-defined empty-palette
   * behavior (W=255 dim white) rather than each picking their own.
   */
  Color getRandomColor();

  /**
   * First palette color if any, else the provided fallback. Helper
   * used by expressions that want a deterministic first-color pick
   * without an explicit if-empty branch at every call site.
   */
  Color firstColorOr(Color fallback) const;

  // Shared safe fallback color when a palette is unconfigured. W channel only
  // so the lamp emits a dim white rather than going dark.
  static inline const Color kSafeFallbackColor{0, 0, 0, 255};

  const std::vector<Color>& getColors() const { return colors; }
  ExpressionTarget getTarget() const { return target; }

  /**
   * True once the expression has finished at least one animation cycle
   * and is back in STOPPED. Used by ExpressionManager's transient GC
   * to know when a remote-cascaded one-shot can be removed from the
   * compositor and destroyed. Defaults are STOPPED + lastCompletedLoop=0
   * on a fresh instance, so this returns false until trigger() has
   * fired AND that firing has completed.
   */
  bool isAnimationComplete() const {
    return animationState == STOPPED && lastCompletedLoop > 0;
  }

  // Suppresses auto-trigger from control() while true. Manual trigger() and
  // chain-triggered firing still work. Listing's enabled toggle drives this.
  bool autoTriggerEnabled = true;

  // Suppresses auto-trigger from control() while the wisp is actively
  // overriding the lamp's base or shade surface. Manual trigger() (the
  // app's "Test" button + chain triggers) still fires. Pure type-property,
  // overridden in subclasses, NOT stored in config/NVS/BLE. Whether a given
  // expression coexists with wisp paint is a property of the expression
  // class itself (continuous animations like breathing / shifty fight the
  // wisp's hold colour and must pause; short discrete ones like glitchy /
  // pulse coexist fine). The control() implementation queries the override
  // state via `isWispCurrentlyOverriding()` (declared in expression.cpp).
  virtual bool disabledDuringWispOverride() const { return false; }

protected:
  /**
   * control() body for continuous expressions. Applies the
   * wisp-override gate every tick (the base-class gate runs only at
   * trigger time) and retriggers when STOPPED. Transients
   * (autoTriggerEnabled=false) never retrigger, so gcTransients()
   * can reap them. Returns true while the wisp override suppresses
   * this expression; on that path a steady-state caller resets its
   * wall-clock state (resume must not integrate the whole hold), and
   * a transient keeps its completion progress advancing so it still
   * reaches STOPPED.
   */
  bool continuousControl();

  /**
   * Expression-specific setup when triggered (REQUIRED).
   * Called when expression starts (both manual and automatic triggers).
   * Implement this to set up colors, state, etc.
   */
  virtual void onTrigger() = 0;

  /**
   * Per-frame update during animation (OPTIONAL).
   * Called every frame while animationState == PLAYING.
   * Implement this for continuous effects like moving waves.
   */
  virtual void onUpdate() { }

  /**
   * Cleanup when animation completes (OPTIONAL).
   * Called when animation finishes.
   * Implement this for state cleanup or chaining effects.
   */
  virtual void onComplete() { }
};

}  // namespace lamp
