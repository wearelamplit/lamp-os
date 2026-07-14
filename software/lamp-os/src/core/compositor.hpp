#pragma once

#include <vector>

#include "animated_behavior.hpp"
#include "behavior_context.hpp"
#include "frame_buffer.hpp"

#define MINIMUM_FRAME_DRAW_TIME_MS 16
#define STARTUP_ANIMATION_FRAMES 120
#define REBOOT_ANIMATION_FRAMES 120

namespace lamp {
/**
 * @brief the compositor singleton coordinates a list of behaviors in the order
 *        they need to draw
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

 public:
  std::vector<AnimatedBehavior*> underlayBehaviors;
  std::vector<AnimatedBehavior*> startupBehaviors;
  std::vector<AnimatedBehavior*> behaviors;
  std::vector<AnimatedBehavior*> overlayBehaviors;
  std::vector<FrameBuffer*> frameBuffers;
  bool startupComplete = false;
  bool behaviorsComputed = false;
  unsigned long lastDrawTimeMs = 0;
  bool homeMode = false;

  Compositor();

  /**
   * @brief initializer
   * @param [in] inBehaviors list of behaviors to execute in priority sequence. last item
   *             is highest priority
   * @param [in] inFrameBuffers of all frame buffers used by the lamp - this helps to
   *             support multi strip lamps
   * @param [in] inUnderlayBehaviors caller-provided underlay behaviors (e.g. IdleBehavior),
   *             one per frame buffer. The compositor wires context and stores the pointers.
   * @param [in] inStartupBehaviors caller-provided startup behaviors (e.g. FadeInBehavior),
   *             one per frame buffer. Played instead of the normal behavior stack until
   *             startupComplete is set.
   * @param [in] homeMode if true only behaviors allowedInHomeMode=true will run to reduce distraction at home
   */
  void begin(std::vector<AnimatedBehavior*> inBehaviors,
             std::vector<FrameBuffer*> inFrameBuffers,
             std::vector<AnimatedBehavior*> inUnderlayBehaviors,
             std::vector<AnimatedBehavior*> inStartupBehaviors,
             bool homeMode = false);

  /**
   * @brief manage building frames and drawing them on the LEDs
   */
  void tick();

  /**
   * @brief update home mode state dynamically
   * @param [in] homeMode new home mode state
   */
  void setHomeMode(bool homeMode);

  /**
   * @brief mark the expression band bounds [start, end) within the
   *        behavior list, keeping the band contiguous as behaviors are
   *        added or removed at runtime.
   */
  void setExpressionBand(size_t start, size_t end);

  /**
   * @brief insert a new expression behavior at the expression-band end.
   *        Increments the band-end index.
   */
  void addBehavior(AnimatedBehavior* b);

  /**
   * @brief insert a base-scene behavior at the expression-band start so it
   *        draws below every expression (a variant's custom base scene, e.g.
   *        snafu's dots). Shifts the whole band up by one.
   */
  void addBaseBehavior(AnimatedBehavior* b);

  /**
   * @brief remove a behavior pointer. If it was within the expression band,
   *        decrements the band-end index.
   */
  void removeBehavior(AnimatedBehavior* b);

  /**
   * @brief Mutable access to the shared BehaviorContext. Used by
   *        lamp.cpp at boot to wire the ExpressionManager pointer
   *        and let ExpressionManager publish its frame buffer list. The
   *        compositor itself self-publishes via the constructor.
   */
  BehaviorContext& behaviorContext() { return context_; }
};
}  // namespace lamp
