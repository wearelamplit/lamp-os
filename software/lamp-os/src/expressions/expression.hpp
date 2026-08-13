#pragma once

#include <cstdint>
#include <map>
#include <variant>
#include <vector>

#include "core/animated_behavior.hpp"
#include "expressions/expression_schema.hpp"
#include "util/color.hpp"
#include "util/easing.hpp"
#include "util/fast_rng.hpp"

namespace lamp {

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
  uint8_t opacityPct_ = 100;
  Easing easing_ = Easing::Linear;
  uint32_t easingRaw_ = 0;
  uint8_t previewCyclesDone_ = 0;

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

  /**
   * Per-pixel contribution scale in [0,1] for a yielding expression drawing
   * on this fb's surface: opacityPct_/100 capped further by the compositor's
   * wisp presence `w`, lerping toward wispDimFloor() as w rises. Opacity 100
   * with no wisp present (or a type that doesn't yield) returns 1.0. Reads
   * the same `w` that composites the wisp, so on release the expression
   * returns to its opacity cap in lockstep with the wisp fade. Per-surface:
   * base presence for a base fb, shade presence for a shade fb.
   */
  float wispDimScale();

  // Read the shared "opacity" param (clamped 10..100, default 100) into
  // opacityPct_. Call from each subclass configureFromParameters.
  void configureOpacity(const std::map<std::string, uint32_t>& parameters);

  // Read the shared "easing" param (default defVal). Resolves Random to a
  // concrete curve immediately; trigger() re-rolls it on each fire.
  void configureEasing(const std::map<std::string, uint32_t>& parameters, uint32_t defVal);

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
   *         buffer, or the operator has this surface's color editor open)
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

  // Floor this expression's per-pixel contribution dims to while the wisp is
  // fully present, in [0,1]. 1.0 (default) ignores the wisp and paints at full
  // regardless. A continuous color animation that would fight the wisp's hold
  // (breathing / shifty / spotty) returns a low floor so it dims-and-blends
  // instead of pausing, and eases back to full with the wisp fade on release.
  // Pure type-property, NOT stored in config/NVS/BLE.
  virtual float wispDimFloor() const { return 1.0f; }

protected:
  /**
   * control() body for continuous expressions. Retriggers when STOPPED so an
   * auto-trigger loop keeps running; transients (autoTriggerEnabled=false)
   * never retrigger, so gcTransients() can reap them.
   */
  void continuousControl();

  /**
   * Call once per completed cycle from a continuous expression's draw().
   * Returns true once a transient preview (autoTriggerEnabled=false) has run
   * kPreviewCycles cycles and should stop for gcTransients(); always false for
   * a live saved instance, which never completes.
   */
  bool previewCycleComplete();

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
