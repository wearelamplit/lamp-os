#ifndef LAMP_BEHAVIORS_SOCIAL_H
#define LAMP_BEHAVIORS_SOCIAL_H

#include "../components/network/bluetooth.hpp"
#include "../core/animated_behavior.hpp"
#include "../util/color.hpp"

#define LAMP_TIME_BETWEEN_ACKNOWLEDGEMENT_MS 30000

/**
 * @brief social color exchange
 */
namespace lamp {
class SocialBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  // how many frames to ease when greeting and returning to the lamp's
  // personality- the total frame count must be a multiple of the ease frames
  uint32_t easeFrames = 120;
  uint32_t nextAcknowledgeTimeMs = 0;
  Color foundLampColor;
  BluetoothComponent* bt;
  std::vector<BluetoothLampRecord>* foundLamps;

  void draw() override;

  void control() override;

  void setBluetoothComponent(BluetoothComponent* inBt);
};
}  // namespace lamp
#endif