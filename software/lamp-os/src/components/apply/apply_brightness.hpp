// Brightness mutation helpers for the "always save, no preview" model.
// Three entry points sharing one signature so call sites declare intent:
//
//   brightnessToConfig:   user-direct BLE write (slider live-preview).
//                         Mutates config + seeds the micro-fade triple
//                         for smooth slider drags + applies initial sample.
//   brightnessToRender:   mesh-relayed CONTROL_OP (cascade brightness).
//                         Render-only: applies setBrightness directly, NO
//                         config mutation, NO fade triple. The cascade is
//                         transient; persisting it would contaminate
//                         CHAR_COMMIT's next persistence sweep.
//   brightnessImmediate:  settings_blob path. Mutates config + applies
//                         via applyEffectiveBrightness, NO fade triple
//                         (settings_blob is a saved value, not a tick).
//                         Resets s_userBrightnessSeeded so the next slider
//                         drag re-seeds cleanly from the new persisted
//                         level instead of rubber-banding from a stale
//                         source.
//
// Firmware-only: Adafruit_NeoPixel in the include chain makes this
// unlinkable from the native test env.
//
// Dependencies pulled in by the include chain:
//   util/levels.hpp        lamp::calculateBrightnessLevel
//   config/config.hpp      lamp::Config (config lives at ::config in lamp.cpp)

#pragma once

#include <Adafruit_NeoPixel.h>
#include <cstdint>

#include "config/config.hpp"
#include "util/levels.hpp"

namespace lamp { void setAllStripsBrightness(uint8_t scaledLevel); }
// config is defined as `lamp::Config config;` at file scope in
// lamp.cpp; it lives at ::config, not ::lamp::config.
extern lamp::Config config;

namespace lamp {

// Micro-fade triple, file-static in lamp.cpp. Exposed via these
// accessors so apply_brightness.hpp can read/write without pulling in
// the rest of the file. Definitions live inside `namespace lamp { ... }`
// in lamp.cpp.
uint8_t  brightnessFadeSource();
uint8_t  brightnessFadeTarget();
uint32_t brightnessFadeStartMs();
bool     brightnessFadeSeeded();
void     setBrightnessFade(uint8_t source, uint8_t target, uint32_t startMs);
void     clearBrightnessFadeSeed();
void     settleBrightnessFade();
uint8_t  computeUserBrightnessNow(uint32_t nowMs);

// Bookkeeping the brightness drain performs.
void stampConfiguratorActivity(uint32_t nowMs);

// Apply effective brightness immediately (no fade). Calls into the
// existing routing/dimming logic. Used by brightnessImmediate.
void applyEffectiveBrightness();

namespace apply {

// User-direct BLE write. Routes to homeMode.brightness vs lamp.brightness
// based on isHomeMode flag; seeds the micro-fade triple; applies the
// initial sample so the strip starts moving immediately.
inline void brightnessToConfig(uint8_t level, bool isHomeMode, uint8_t maxBrightness) {
  if (isHomeMode) {
    ::config.homeMode.brightness = level;
  } else {
    ::config.lamp.brightness = level;
  }
  const uint32_t fadeNow = ::millis();
  ::lamp::stampConfiguratorActivity(fadeNow);
  const uint8_t source = ::lamp::brightnessFadeSeeded()
                             ? ::lamp::computeUserBrightnessNow(fadeNow)
                             : level;
  ::lamp::setBrightnessFade(source, level, fadeNow);
  // Apply initial sample so the strip starts moving this drain cycle.
  // (The compositor's per-tick interpolation handles the rest.)
  ::lamp::setAllStripsBrightness(
      ::lamp::calculateBrightnessLevel(maxBrightness, source));
}

// Mesh-relayed cascade brightness. Render-only: snap setBrightness,
// no config mutation, no fade triple. Skipping the fade gives the
// cascade its "instant change" UX (matches today's social-greet behavior
// for shade/base color cascades).
inline void brightnessToRender(uint8_t level, bool isHomeMode, uint8_t maxBrightness) {
  (void)isHomeMode;  // Cascade brightness isn't home-mode-routed.
  ::lamp::setAllStripsBrightness(
      ::lamp::calculateBrightnessLevel(maxBrightness, level));
}

// settings_blob path. Saved value semantics, no fade UX. Mutates
// config, applies via applyEffectiveBrightness (which respects the
// dimming/social-disposition multipliers), and resets the slider fade
// seed so a subsequent slider drag starts from the new persisted level.
inline void brightnessImmediate(uint8_t level, bool isHomeMode, uint8_t maxBrightness) {
  (void)maxBrightness;
  if (isHomeMode) {
    ::config.homeMode.brightness = level;
  } else {
    ::config.lamp.brightness = level;
  }
  ::lamp::clearBrightnessFadeSeed();
  ::lamp::applyEffectiveBrightness();
}

}  // namespace apply
}  // namespace lamp
