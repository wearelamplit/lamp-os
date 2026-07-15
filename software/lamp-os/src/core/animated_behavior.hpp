#pragma once

#include <cstdint>

#include "behavior_context.hpp"
#include "frame_buffer.hpp"

namespace lamp {
enum AnimationState {
  // Animation is playing normally
  PLAYING = 1,

  // Animation is about to stop dead in its tracks
  PAUSING = 2,

  // Animation does not contribute pixels; playhead holds at last frame
  PAUSED = 3,

  // Animation stops gracefully; lets the current frame count finish
  STOPPING = 4,

  // Animation does not contribute pixels; resumes from frame 0 on play
  STOPPED = 5,

  // Animation is playing only one loop
  PLAYING_ONCE = 6
};

/**
 * An Animated behavior is any lamp behavior that will have a side effect on the
 * LEDs. Any animation will run at roughly 60 frames per second
 */
class AnimatedBehavior {
 public:
  FrameBuffer* fb;
  uint32_t frames = 60;
  uint32_t frame = 0;
  uint32_t currentLoop = 0;
  bool allowedInHomeMode = true;
  AnimationState animationState = STOPPED;

  /**
   * Animated Behavior Base class - integrators implement draw and control
   * functions of their own to control the lamp's LEDs
   * @param [in] inBuffer the frame buffer to interact with
   * @param [in] inFrames the frame duration for the behaviour eg (60 frames ~ 1
   *                      second of animation)
   * @param [in] autoPlay if true the animation will begin immediately
   */
  AnimatedBehavior();
  AnimatedBehavior(FrameBuffer* inBuffer, uint32_t inFrames = 60, bool inAutoPlay = false);
  virtual ~AnimatedBehavior();

  /**
   * @brief A virtual function to make changes to the frame buffer per frame
   */
  virtual void draw();

  /**
   * @brief A virtual function to do calculations and coordinate animation state
   *        for each animation layer
   */
  virtual void control();

  /**
   * @brief Pause the animation and redraw the paused frame
   */
  void pause();

  /**
   * @brief Stop the animation at the last frame
   */
  void stop();

  /**
   * @brief Play the animation in a loop
   */
  void play();

  /**
   * @brief Play the animation for one full frame cycle
   */
  void playOnce();

  /**
   * @brief Check for the last frame
   * @return true if the playhead is at the last frame of the animation
   */
  bool isLastFrame();

  /**
   * @brief conclude the draw procedure and advance the internal frame counters
   */
  void nextFrame();

  /**
   * @brief Pixel count of the wired frame buffer, floored at 1 so callers can
   *        divide or clamp without a null / zero guard.
   */
  uint16_t windowSize() const;

  /**
   * @brief Wire a BehaviorContext into this behavior. Called by the Compositor
   *        the moment a behavior is registered (via addBehavior or begin()),
   *        and by ExpressionManager just before handing a transient to the
   *        compositor. The context is owned by the Compositor; behaviors only
   *        hold a non-owning pointer.
   */
  void setBehaviorContext(BehaviorContext* ctx) { context_ = ctx; }
  BehaviorContext* behaviorContext() const { return context_; }

 protected:
  // Non-owning. Null until the Compositor (or ExpressionManager, for
  // transients) wires it during registration. Consumers must null-check.
  BehaviorContext* context_ = nullptr;
};
}  // namespace lamp
