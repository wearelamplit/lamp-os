#include "behaviors/zone_preview_surface.hpp"

#include "expressions/zone_preview.hpp"

namespace lamp {

void ZonePreviewSurface::show(ConfiguratorBehavior& cfg, uint16_t pixelCount, uint32_t posMin,
                              uint32_t posMax, Color color) {
  if (pixelCount == 0) return;
  if (!active_) {
    saved_ = cfg.scene();
    active_ = true;
  }
  cfg.beginFade(buildZonePreviewBuffer(pixelCount, posMin, posMax, color), 0);
}

void ZonePreviewSurface::release(ConfiguratorBehavior& cfg) {
  if (!active_) return;
  cfg.showScene(saved_, 0);
  saved_.colors.clear();
  active_ = false;
}

}  // namespace lamp
