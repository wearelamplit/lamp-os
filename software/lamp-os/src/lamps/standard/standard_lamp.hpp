#pragma once

#include <ArduinoJson.h>

#include "core/lamp.hpp"

#define LAMP_SHADE_PIN 12
#define LAMP_BASE_PIN 14
#define LAMP_MAX_BRIGHTNESS 230

namespace lamp {

class StandardLamp : public Lamp {
 public:
  StandardLamp() : Lamp(HwConfig{
    .strips = {
      {.role=Surface::Shade, .pin=LAMP_SHADE_PIN, .byteOrder=ByteOrder::GRBW, .name="Shade", .broadcast=1},
      {.role=Surface::Base,  .pin=LAMP_BASE_PIN,  .byteOrder=ByteOrder::GRBW, .name="Base", .reversed=true},
    },
    .maxBrightness = LAMP_MAX_BRIGHTNESS,
    .supplyBudgetMa = 1400,
  }) {}

 protected:
  Features featuresEnabled() const override { return Features::All; }

  Config::Defaults defaults() const override {
    return {
      // name omitted-no: kept as "stray" because lamp.cpp's first-boot
      // setup-flag detection compares config.lamp.name == defaults().name.
      .name = "stray",
      // baseColor + *ColorsEditable omitted: they equal the Config class
      // defaults, and first-boot randomization (lamp.cpp) overwrites the
      // colors anyway. shadeColor differs from the class default, so it
      // stays as the pre-randomization baseline.
      .shadeColor = "#5A170000",
      .basePx  = 35,
      .shadePx = 38,
    };
  }

  void createBehaviors(BehaviorStackBuilder&) override {
    // All StandardLamp behaviors are covered by Feature flags (Features::All).
    // Nothing to add here; the framework registers configurator, personality,
    // social, fade-out, and knockout based on featuresEnabled().
  }
};

}  // namespace lamp
