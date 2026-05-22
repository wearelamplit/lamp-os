#ifndef LAMP_BEHAVIORS_DMX_H
#define LAMP_BEHAVIORS_DMX_H

#include <cstdint>

#include "../components/network/wifi.hpp"
#include "../core/animated_behavior.hpp"
#include "../util/color.hpp"

#define DMX_ARTNET_TIMEOUT_MS 30000
#define DMX_BEHAVIOR_FADE_TIME_MIN_FRAMES 180
#define DMX_BEHAVIOR_FADE_TIME_MAX_FRAMES 700
#define DMX_BEHAVIOR_FADE_TIME_LOW_STEPS_FRAMES 320

/**
 * @brief a layer to take colors from artnet periodically and
 *        shift the lamp's colors slowly to match the stage at random
 *        intervals
 */
namespace lamp {
class DmxBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 private:
  uint8_t i = 0;

 public:
  // the total frame count must be a multiple of the ease frames
  uint32_t easeFrames = 120;

  // the incoming color from DMX every few ms to sample from
  Color currentDmxColor = Color();

  // the lamp's currently calculated color
  Color currentColor = Color();

  // the lamp's selected color ranges to blur between
  Color startColor = Color();
  Color endColor = Color();
  uint32_t transitionFrames = 0;
  uint32_t transitionFrame = 0;

  // keep track if artnet input is still incoming
  uint32_t lastArtnetFrameTimeMs = 0;

  void draw();

  void control();

  /**
   * @brief Set the DMX current color from the DMX Callback
   * @param [in] inColor the realtime color found
   */
  void setColor(Color inColor);

  /**
   * @brief Set the last known Artnet frame callback time
   * @param [in] inLastArtnetFrameTimeMs a time in milliseconds
   */
  void setLastArtnetFrameTimeMs(uint32_t inLastArtnetFrameTimeMs);
};
}  // namespace lamp
#endif