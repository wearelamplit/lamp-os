// The wispConfig fields WispConfig holds. Header-only so native tests reach
// it; StatusEmitter adds the network-derived `ip`.

#pragma once

#include <lampos/led_types.hpp>

#include "config/wisp_config.hpp"
#include "status/status_json.hpp"

namespace wisp {

// The result points into `config` (ledType, oscAddr), so it must not outlive
// it.
inline WispConfigFields wispConfigFields(const WispConfig& config) {
  WispConfigFields c;
  c.ledType = lampos::led::byteOrderToString(config.ledFormat());
  c.pixelCount = config.pixelCount();
  c.brightness = config.brightness();
  c.driftIntervalMs = config.driftIntervalMs();
  c.driftFadePct = config.driftFadePct();
  c.shuffleSeed = config.shuffleSeed();
  c.opSeq = config.opSeq();
  c.swatches = config.paletteStyle() == PaletteStyle::Swatches;
  c.oscMode = config.oscMode();
  c.oscSlot = config.oscSlot();
  c.oscAddr = config.oscAddress();
  return c;
}

}  // namespace wisp
