// software/lamp-os/src/core/override_aggregate.hpp
//
// Bundles the three transient overrides that used to live at file scope
// in lamp.cpp into a single aggregate the framework owns. File-scope
// per the single-instance-per-binary design (see lamp.cpp header
// comment).

#pragma once

#include "components/transient_override/color_override.hpp"
#include "components/transient_override/brightness_override.hpp"

namespace lamp {

struct OverrideAggregate {
  ColorOverride base;
  ColorOverride shade;
  BrightnessOverride brightness;
};

// Single production instance.
extern OverrideAggregate overrides;

}  // namespace lamp
