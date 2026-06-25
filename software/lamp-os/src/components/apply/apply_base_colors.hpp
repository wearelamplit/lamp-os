// software/lamp-os/src/components/apply/apply_base_colors.hpp
//
// Base-color mutation helpers, user/remote split. See apply_shade_colors.hpp
// for the rationale.
//
//   baseColorsToConfig  — user-direct BLE write. Mutates config.base.colors
//                         so a later CHAR_COMMIT can persist the user's
//                         choice. Then calls the existing render path.
//   baseColorsToRender  — mesh-relayed cascade. Render-only. Matches
//                         today's applyBaseColorsLocal behavior exactly.

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
void renderBaseColors(JsonArray arr);

namespace apply {

// User-source variant. Mutates config.base.colors first so a concurrent
// CHAR_COMMIT (next loop tick) sees the new value, then delegates to the
// render path for the visual update.
inline void baseColorsToConfig(JsonArray arr) {
  if (arr.isNull() || arr.size() == 0) return;
  std::vector<Color> next;
  next.reserve(arr.size());
  for (JsonVariant v : arr) {
    next.push_back(hexStringToColor(v));
  }
  ::config.base.colors = next;
  ::lamp::renderBaseColors(arr);
}

// Cascade-source variant. Render-only — today's exact behavior.
// Does NOT mutate config so cascade transients cannot contaminate a
// subsequent CHAR_COMMIT persistence sweep.
inline void baseColorsToRender(JsonArray arr) {
  ::lamp::renderBaseColors(arr);
}

}  // namespace apply
}  // namespace lamp
