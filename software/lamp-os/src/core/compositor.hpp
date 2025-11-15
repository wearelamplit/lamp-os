#ifndef LAMP_CORE_COMPOSITOR_H
#define LAMP_CORE_COMPOSITOR_H

#include <vector>

#include "./animated_behavior.hpp"
#include "./frame_buffer.hpp"

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
  uint8_t i = 0;

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
  AnimatedBehavior* activeExclusive = nullptr;  // Currently running exclusive behavior

  Compositor();

  /**
   * @brief initializer
   * @param [in] inBevaviors list of behaviors to execute in priority sequence. last item
   *             is highest priority
   * @param [in] inFrameBuffers of all frame buffers used by the lamp - this helps to
   *             support multi strip lamps
   * @param [in] homeMode if true only behaviors allowedInHomeMode=true will run to reduce distraction at home
   */
  void begin(std::vector<AnimatedBehavior*> inBehaviors, std::vector<FrameBuffer*> inFrameBuffers, bool homeMode = false);

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
   * @brief check if an exclusive behavior is currently active
   * @return true if an exclusive behavior is running
   */
  bool hasActiveExclusive() const;
};
}  // namespace lamp
#endif