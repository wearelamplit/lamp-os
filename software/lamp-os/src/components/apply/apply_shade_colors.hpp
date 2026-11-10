// Shade-color mutation helpers, user/remote split.
//
//   shadeColorsToConfig: user-direct BLE write (live-preview editor
//                        drag). Updates config.shade.colors so a later
//                        CHAR_COMMIT can persist the user's choice.
//                        Then calls the existing render path.
//   shadeColorsToRender: the app's CHAR_REMOTE_OP write. Render-only. Matches
//                        applyShadeColorsLocal behavior exactly.

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
void renderShadeColors(JsonArray arr);

namespace apply {

// User-source variant. Mutates config.shade.colors first so a concurrent
// CHAR_COMMIT (next loop tick) sees the new value, then delegates to the
// render path for the visual update.
inline void shadeColorsToConfig(JsonArray arr) {
  if (arr.isNull() || arr.size() == 0) return;
  std::vector<Color> next;
  next.reserve(std::min(arr.size(), kMaxSurfaceColors));
  for (JsonVariant v : arr) {
    if (next.size() == kMaxSurfaceColors) break;
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
  next.reserve(std::min(arr.size(), kMaxSurfaceColors));
  for (JsonVariant v : arr) {
    if (next.size() == kMaxSurfaceColors) break;
    next.push_back(hexStringToColor(v));
  }
  ::config.shade.segments[seg].colors = next;
  if (seg == 0) ::lamp::renderShadeColors(arr);
}

// A CHAR_SHADE_COLORS write: a bare colour array, {"seg":k,"colors":[...]},
// or {"colors":[...],"rotate":{...}}. `rotate` reaches RAM config before the
// render, and only on a rotatable role with seg 0 or no seg. An object without
// a non-empty `colors` array changes nothing. Persists nothing.
inline void shadeColorsWriteToConfig(JsonVariant payload) {
  if (payload.is<JsonArray>()) {
    shadeColorsToConfig(payload.as<JsonArray>());
    return;
  }
  if (!payload.is<JsonObject>()) return;
  JsonArray colors = payload["colors"];
  if (colors.isNull() || colors.size() == 0) return;
  const int seg = payload["seg"] | 0;
  if (seg == 0 && ::config.shade.rotatable()) {
    config_codec::parseRotation(payload["rotate"], ::config.shade.rotation);
  }
  shadeSegmentColorsToConfig(seg, colors);
}

// Remote-op variant. Render-only.
// Does NOT mutate config so a preview cannot contaminate a
// subsequent CHAR_COMMIT persistence sweep.
inline void shadeColorsToRender(JsonArray arr) {
  ::lamp::renderShadeColors(arr);
}

}  // namespace apply
}  // namespace lamp
