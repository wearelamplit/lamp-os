// settings_blob's `homeMode` section handler. Updates config.homeMode.*
// fields in place. Missing fields are left alone (partial-merge by design).
//
// HomeModeSettings has no `password` field: the lamp only sniffs WiFi
// beacons for SSID presence and never associates to the AP, so no
// credential is stored or transmitted. See config_types.hpp for the
// constraint.

#pragma once

#include <ArduinoJson.h>

#include "config/config.hpp"
#include "components/apply/apply_brightness.hpp"

// config is defined as `lamp::Config config;` at file scope in
// lamp.cpp; it lives at ::config, not ::lamp::config.
extern lamp::Config config;

namespace lamp {
namespace apply {

// Applies all writable fields in the `homeMode` JSON object to config and
// to runtime state. Missing fields are left alone (settings_blob is
// partial-merge by design; caller omits what it doesn't want to touch).
inline void homeModeLocal(JsonObject obj, uint8_t maxBrightness) {
  if (obj.isNull()) return;
  if (obj["ssid"].is<const char*>()) {
    ::config.homeMode.ssid = obj["ssid"].as<const char*>();
  }
  if (obj["enabled"].is<bool>()) {
    ::config.homeMode.enabled = obj["enabled"].as<bool>();
  }
  if (obj["networkBound"].is<bool>())
    ::config.homeMode.networkBound = obj["networkBound"].as<bool>();
  if (obj["socialDisabled"].is<bool>())
    ::config.homeMode.socialDisabled = obj["socialDisabled"].as<bool>();
  if (obj["disabledExpressionTypes"].is<JsonArray>()) {
    ::config.homeMode.disabledExpressionTypes.clear();
    for (JsonVariant v : obj["disabledExpressionTypes"].as<JsonArray>())
      ::config.homeMode.disabledExpressionTypes.push_back(std::string(v.as<const char*>()));
  }
  if (obj["brightness"].is<int>()) {
    int level = obj["brightness"].as<int>();
    if (level >= 0 && level <= 100) {
      ::lamp::apply::brightnessImmediate(static_cast<uint8_t>(level),
                                          /*isHomeMode=*/true, maxBrightness);
    }
  }
}

}  // namespace apply
}  // namespace lamp
