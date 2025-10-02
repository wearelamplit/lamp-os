#ifndef LAMP_CORE_ANIMATED_BEHAVIOR_H
#define LAMP_CORE_ANIMATED_BEHAVIOR_H

#include <cstdint>

#include "./frame_buffer.hpp"

namespace lamp {
enum AnimationState {
  // Animation is playing normally
  PLAYING = 1,

  // Animation is about to stop dead in its tracks
  PAUSING = 2,

  // Animation will no longer contribute pixels to the scene and will keep
  // its playhead at the last frame position
  PAUSED = 3,

  // Animation will stop gracefully and let the total frame count continue
  // to contribute to the scene
  STOPPING = 4,

  // Animation will no longer contribute to the scene and it will
  // resume from the beginning of the frame count
  STOPPED = 5,

  // Animation is playing only one loop
  PLAYING_ONCE = 6
};

/**
 * An Animated behavior is any lamp behavior that will have a side effect on the
 * LEDs. Any animation will run at roughly 30 frames per second
 */
class AnimatedBehavior {
 public:
  FrameBuffer* fb;
  uint32_t frames = 60;
  uint32_t frame = 0;
  uint32_t currentLoop = 0;
  bool allowedInHomeMode = true;
  bool isExclusive = false;  // If true, pauses other non-exclusive behaviors
  AnimationState animationState = STOPPED;

  /**
   * Animated Behavior Base class - integrators implement draw and control
   * functions of their own to control the lamp's LEDs
   * @param [in] inBuffer the frame buffer to interact with
   * @param [in] inFrames the frame duration for the behaviour eg (60 frames ~ 2
   *                      seconds of animation)
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
};
}  // namespace lamp

#endif
