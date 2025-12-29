#ifndef LAMP_BEHAVIORS_CONFIGURATOR_H
#define LAMP_BEHAVIORS_CONFIGURATOR_H

#include <cstdint>
#include <vector>

#include "../components/network/wifi.hpp"
#include "../config/config_types.hpp"
#include "../core/animated_behavior.hpp"
#include "../util/color.hpp"

#define CONFIGURATOR_WEBSOCKET_TIMEOUT_MS 60000

/**
 * @brief a layer to preview realtime changes from the web
 *        configuration tool
 */
namespace lamp {
class ConfiguratorBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  // the total frame count must be a multiple of the ease frames
  uint32_t easeFrames = 60;
  uint8_t brightness = 100;
  std::vector<Color> colors;
  unsigned long lastWebSocketUpdateTimeMs = 0;
  bool disabled = false;  // Can be disabled during expression previews

  void draw() override;

  void control() override;
};
}  // namespace lamp
#endif