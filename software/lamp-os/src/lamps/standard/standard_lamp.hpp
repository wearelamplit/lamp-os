#pragma once

#include <ArduinoJson.h>

#include "core/lamp.hpp"

#define LAMP_SHADE_PIN 12
#define LAMP_BASE_PIN 14
#define LAMP_MAX_BRIGHTNESS 180

namespace lamp {

class StandardLamp : public Lamp {
 public:
  StandardLamp() : Lamp(HwConfig{
    .surfaces = {
      {.id=Surface::Shade, .pin=LAMP_SHADE_PIN, .byteOrder=ByteOrder::GRBW},
      {.id=Surface::Base,  .pin=LAMP_BASE_PIN,  .byteOrder=ByteOrder::GRBW},
    },
    .maxBrightness = LAMP_MAX_BRIGHTNESS,
  }) {}

 protected:
  Features featuresEnabled() const override { return Features::All; }

  Config::Defaults defaults() const override {
    return {
      .name = "anonymous-lamp",
      .baseColor  = "#30078300",
      .shadeColor = "#5A170000",
      .baseColorsEditable  = true,
      .shadeColorsEditable = true,
      .basePx  = 36,
      .shadePx = 32,
    };
  }

  void createBehaviors(BehaviorStackBuilder&) override {
    // All StandardLamp behaviors are covered by Feature flags (Features::All).
    // Nothing to add here — the framework registers configurator, personality,
    // social, fade-out, and knockout based on featuresEnabled().
  }
};

}  // namespace lamp
