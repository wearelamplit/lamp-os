#pragma once

#include <string>
#include <vector>

#include "animated_behavior.hpp"
#include "behavior_context.hpp"
#include "frame_buffer.hpp"
#include "home_mode_gate.hpp"

#define MINIMUM_FRAME_DRAW_TIME_MS 16
#define STARTUP_ANIMATION_FRAMES 120
#define REBOOT_ANIMATION_FRAMES 120

namespace lamp {
/**
 * The compositor singleton coordinates a list of behaviors in the order
 * they need to draw.
 */
class Compositor {
 private:
  // Expression-band bounds in `behaviors`, [start, end). Set by the lamp
  // after pushing the initial stack; runtime adds/removes maintain them.
  // addBehavior inserts at `end` (new expressions join the band top);
  // addBaseBehavior inserts at `start` (base-scene behaviours draw below
  // the whole band). Everything below `start` (configurators, base scenes)
  // draws first; expressions compose on top.
  size_t expressionBandStart = 0;
  size_t expressionBandEnd = 0;

  // The shared per-behavior context. Owned here; every registered behavior
  // gets a pointer via setBehaviorContext. The lamp wires the ExpressionManager
  // pointer and frame-buffer list in initBehaviors(). Single instance, multiple readers.
  BehaviorContext context_;

  // Home-mode suppression policy. Updated via setHomeModePolicy().
  bool homeSocialDisabled_ = true;
  std::vector<std::string> homeDisabledExprIds_ = {"glitchy"};

  // Returns true if `b` should be skipped during a home-mode tick.
  bool homeModeSkips(AnimatedBehavior* b) const;

  // true once a quiet-mode frame has painted; the next false->true edge on
  // ota_quiet_mode::isQuiet() signals ota_indicator::paint() to (re)init its
  // base-surface settle.
  bool otaQuietPainted_ = false;

 public:
  std::vector<AnimatedBehavior*> underlayBehaviors;
  std::vector<AnimatedBehavior*> startupBehaviors;
  std::vector<AnimatedBehavior*> behaviors;
  std::vector<AnimatedBehavior*> overlayBehaviors;
  std::vector<FrameBuffer*> frameBuffers;
  // Runs once per drawn frame, after behaviors draw and before any
  // FrameBuffer flush, so a brightness decision (power governor) reaches the
  // drivers ahead of that frame's pixel writes.
  void (*preFlushHook)() = nullptr;
  bool startupComplete = false;
  bool behaviorsComputed = false;
  unsigned long lastDrawTimeMs = 0;
  bool homeMode = false;

  Compositor();

  /**
   * @param [in] inBehaviors list of behaviors to execute in priority sequence. last item
   *             is highest priority
   * @param [in] inFrameBuffers of all frame buffers used by the lamp - this helps to
   *             support multi strip lamps
   * @param [in] inUnderlayBehaviors caller-provided underlay behaviors (e.g. IdleBehavior),
   *             one per frame buffer. The compositor wires context and stores the pointers.
   * @param [in] inStartupBehaviors caller-provided startup behaviors (e.g. FadeInBehavior),
   *             one per frame buffer. Played instead of the normal behavior stack until
   *             startupComplete is set.
   * @param [in] homeMode if true the home-mode suppression policy applies (see setHomeModePolicy)
   */
  void begin(std::vector<AnimatedBehavior*> inBehaviors,
             std::vector<FrameBuffer*> inFrameBuffers,
             std::vector<AnimatedBehavior*> inUnderlayBehaviors,
             std::vector<AnimatedBehavior*> inStartupBehaviors,
             bool homeMode = false);

  /**
   * Build frames and draw them on the LEDs.
   */
  void tick();

  /**
   * Update home mode state dynamically.
   * @param [in] homeMode new home mode state
   */
  void setHomeMode(bool homeMode);

  /**
   * Update the home-mode suppression policy.
   * @param [in] socialDisabled whether social greetings are suppressed in home mode
   * @param [in] disabledIds expression type ids suppressed in home mode
   */
  void setHomeModePolicy(bool socialDisabled, std::vector<std::string> disabledIds);

  /**
   * True when home mode is active AND `type` is in the home-disabled set.
   * Shared with the render-skip decision so the mesh receive path can drop
   * a home-disabled cascade at receipt instead of creating a squatting
   * transient the render gate then refuses to draw.
   */
  bool homeModeSkipsType(const std::string& type) const;

  /**
   * Mark the expression band bounds [start, end) within the
   * behavior list, keeping the band contiguous as behaviors are
   * added or removed at runtime.
   */
  void setExpressionBand(size_t start, size_t end);

  /**
   * Insert a new expression behavior at the expression-band end.
   * Increments the band-end index.
   */
  void addBehavior(AnimatedBehavior* b);

  /**
   * Insert a base-scene behavior at the expression-band start so it
   * draws below every expression (a variant's custom base scene, e.g.
   * snafu's dots). Shifts the whole band up by one.
   */
  void addBaseBehavior(AnimatedBehavior* b);

  /**
   * Remove a behavior pointer. If it was within the expression band,
   * decrements the band-end index.
   */
  void removeBehavior(AnimatedBehavior* b);

  /**
   * Mutable access to the shared BehaviorContext. Used by
   * lamp.cpp at boot to wire the ExpressionManager pointer
   * and let ExpressionManager publish its frame buffer list. The
   * compositor itself self-publishes via the constructor.
   */
  BehaviorContext& behaviorContext() { return context_; }
};
}  // namespace lamp
