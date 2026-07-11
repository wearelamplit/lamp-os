// software/lamp-os/src/components/apply/apply_shade_colors.hpp
//
// Shade-color mutation helpers, user/remote split.
//
//   shadeColorsToConfig — user-direct BLE write (live-preview editor
//                         drag). Updates config.shade.colors so a later
//                         CHAR_COMMIT can persist the user's choice.
//                         Then calls the existing render path.
//   shadeColorsToRender — mesh-relayed cascade. Render-only. Matches
//                         today's applyShadeColorsLocal behavior exactly.

#pragma once

#include <ArduinoJson.h>

#include <vector>

#include "config/config.hpp"
#include "util/color.hpp"

// config is defined as `lamp::Config config;` at file scope in
// lamp.cpp — i.e., it lives at ::config, not ::lamp::config.
extern lamp::Config config;

namespace lamp {

// Provided by lamp.cpp — calls into the existing render path
// (ConfiguratorBehavior beginFade + ColorOverride rebaseline + BLE advert
// update).
void renderShadeColors(JsonArray arr);

namespace apply {

// User-source variant. Mutates config.shade.colors first so a concurrent
// CHAR_COMMIT (next loop tick) sees the new value, then delegates to the
// render path for the visual update.
inline void shadeColorsToConfig(JsonArray arr) {
  if (arr.isNull() || arr.size() == 0) return;
  std::vector<Color> next;
  next.reserve(arr.size());
  for (JsonVariant v : arr) {
    next.push_back(hexStringToColor(v));
  }
  ::config.shade.broadcastColors() = next;
  ::lamp::renderShadeColors(arr);
}

// Per-segment user-source variant. Writes config.shade.segments[seg].colors so
// a segment-aware behaviour (snafu dots) previews the edit live. seg 0 is the
// broadcast segment, so it also drives the render/advert path.
inline void shadeSegmentColorsToConfig(uint8_t seg, JsonArray arr) {
  if (arr.isNull() || arr.size() == 0) return;
  if (seg >= ::config.shade.segments.size()) return;
  std::vector<Color> next;
  next.reserve(arr.size());
  for (JsonVariant v : arr) {
    next.push_back(hexStringToColor(v));
  }
  ::config.shade.segments[seg].colors = next;
  if (seg == 0) ::lamp::renderShadeColors(arr);
}

// Cascade-source variant. Render-only — today's exact behavior.
// Does NOT mutate config so cascade transients cannot contaminate a
// subsequent CHAR_COMMIT persistence sweep.
inline void shadeColorsToRender(JsonArray arr) {
  ::lamp::renderShadeColors(arr);
}

}  // namespace apply
}  // namespace lamp
