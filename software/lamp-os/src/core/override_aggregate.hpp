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
};

// Single production instance.
extern OverrideAggregate overrides;

}  // namespace lamp
