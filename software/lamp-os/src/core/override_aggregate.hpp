// software/lamp-os/src/core/override_aggregate.hpp
//
// Bundles the three transient overrides into a single aggregate owned
// by the framework. File-scope per the single-instance-per-binary
// design (see lamp.cpp).

#pragma once

#include "components/transient_override/color_override.hpp"
#include "components/transient_override/brightness_override.hpp"

namespace lamp {

struct OverrideAggregate {
  ColorOverride base;
  ColorOverride shade;
  BrightnessOverride brightness;

  // Suspend autonomous render (wisp paint + expressions) on the masked
  // surfaces while an operator previews a static overlay (color edit or
  // zone highlight), so the overlay shows through. base=0x01, shade=0x02.
  void setSurfacePreview(uint8_t surfaceMask, bool previewing) {
    if (surfaceMask & 0x01) base.setOperatorEditing(previewing);
    if (surfaceMask & 0x02) shade.setOperatorEditing(previewing);
  }
};

// Single production instance.
extern OverrideAggregate overrides;

}  // namespace lamp
