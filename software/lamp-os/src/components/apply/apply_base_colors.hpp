// Base-color mutation helpers, user/remote split. See apply_shade_colors.hpp
// for the rationale.
//
//   baseColorsToConfig: user-direct BLE write. Mutates config.base.colors
//                       so a later CHAR_COMMIT can persist the user's
//                       choice. Then calls the existing render path.
//   baseColorsToRender: the app's CHAR_REMOTE_OP write. Render-only. Matches
//                       applyBaseColorsLocal behavior exactly.

#pragma once

#include <ArduinoJson.h>

#include <algorithm>

#include <vector>

#include "config/config.hpp"
#include "config/config_codec.hpp"
#include "util/color.hpp"

// config is defined as `lamp::Config config;` at file scope in
// lamp.cpp; it lives at ::config, not ::lamp::config.
extern lamp::Config config;

namespace lamp {

// Provided by lamp.cpp. Calls into the existing render path
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
  next.reserve(std::min(arr.size(), kMaxSurfaceColors));
  for (JsonVariant v : arr) {
    if (next.size() == kMaxSurfaceColors) break;
    next.push_back(hexStringToColor(v));
  }
  ::config.base.broadcastColors() = next;
  ::lamp::renderBaseColors(arr);
}

// A CHAR_BASE_COLORS write: a bare colour array or
// {"colors":[...],"rotate":{...}}. `rotate` reaches RAM config before the
// render, and only on a rotatable role. The base has no per-segment form, so
// an object with a `seg` other than 0 changes nothing, as does one without a
// non-empty `colors` array. Persists nothing.
inline void baseColorsWriteToConfig(JsonVariant payload) {
  if (payload.is<JsonArray>()) {
    baseColorsToConfig(payload.as<JsonArray>());
    return;
  }
  if (!payload.is<JsonObject>()) return;
  JsonArray colors = payload["colors"];
  if (colors.isNull() || colors.size() == 0) return;
  JsonVariant seg = payload["seg"];
  if (!seg.isNull() && !(seg.is<uint8_t>() && seg.as<uint8_t>() == 0)) return;
  if (::config.base.rotatable()) {
    config_codec::parseRotation(payload["rotate"], ::config.base.rotation);
  }
  baseColorsToConfig(colors);
}

// Remote-op variant. Render-only.
// Does NOT mutate config so a preview cannot contaminate a
// subsequent CHAR_COMMIT persistence sweep.
inline void baseColorsToRender(JsonArray arr) {
  ::lamp::renderBaseColors(arr);
}

}  // namespace apply
}  // namespace lamp
