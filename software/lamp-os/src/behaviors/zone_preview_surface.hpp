#pragma once

#include <cstdint>

#include "behaviors/configurator.hpp"
#include "util/color.hpp"
#include "util/color_rotation.hpp"

namespace lamp {

// One surface's live Zone marker from the expression editor. Release puts back
// the scene the configurator showed when the marker first went up, rotating on
// from where the marker stopped it.
class ZonePreviewSurface {
 public:
  // Paints [posMin,posMax] in `color` and every other pixel off.
  void show(ConfiguratorBehavior& cfg, uint16_t pixelCount, uint32_t posMin, uint32_t posMax,
            Color color);
  // Does nothing while no marker is up.
  void release(ConfiguratorBehavior& cfg);
  bool active() const { return active_; }

 private:
  bool active_ = false;
  RoleScene saved_;
};

}  // namespace lamp
