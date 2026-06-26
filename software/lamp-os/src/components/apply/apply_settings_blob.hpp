// software/lamp-os/src/components/apply/apply_settings_blob.hpp
//
// settings_blob's per-section dispatch orchestrator. Returns whether
// the incoming blob requested a reboot. The caller (settings_blob drain
// in lamp.cpp) handles persistConfig + cache invalidation +
// notifyStateChange + the fadeOutReboot flag.

#pragma once

#include <ArduinoJson.h>

#include "components/apply/apply_lamp.hpp"
#include "components/apply/apply_home_mode.hpp"
#include "components/apply/apply_base_ac.hpp"
#include "components/apply/apply_base_knockout.hpp"
#include "components/apply/apply_shade_colors.hpp"
#include "components/apply/apply_base_colors.hpp"
#include "components/apply/apply_pixel_format.hpp"

namespace lamp {
namespace apply {

// Dispatches each top-level section in the incoming blob to its
// applyXxxLocal handler. Returns whether the caller should reboot
// after persisting. `expressions` is INTENTIONALLY SKIPPED — the
// per-entry CHAR_EXPRESSION_OP path is canonical for that section.
// `factoryReset` is handled by the caller before this orchestrator
// runs (short-circuit).
//
// Caller pre-checks (NOT this helper's responsibility):
//   - OTA in progress → discard, do not call this helper
//   - factoryReset key → wipe NVS + reboot, do not call this helper
inline bool settingsBlobLocal(JsonObject doc, uint8_t maxBrightness) {
  if (doc.isNull()) return false;

  if (doc["lamp"].is<JsonObject>()) {
    apply::lampLocal(doc["lamp"].as<JsonObject>(), maxBrightness);
  }
  if (doc["homeMode"].is<JsonObject>()) {
    apply::homeModeLocal(doc["homeMode"].as<JsonObject>(), maxBrightness);
  }
  if (doc["base"].is<JsonObject>()) {
    JsonObject baseObj = doc["base"].as<JsonObject>();
    apply::pixelFormatLocal(baseObj, ::config.base);
    if (baseObj["colors"].is<JsonArray>()) {
      apply::baseColorsToConfig(baseObj["colors"].as<JsonArray>());
    }
    if (baseObj["ac"].is<int>()) {
      apply::baseAcLocal(baseObj["ac"].as<int>());
    }
    if (baseObj["knockout"].is<JsonArray>()) {
      apply::baseKnockoutLocal(baseObj["knockout"].as<JsonArray>());
    }
  }
  if (doc["shade"].is<JsonObject>()) {
    JsonObject shadeObj = doc["shade"].as<JsonObject>();
    apply::pixelFormatLocal(shadeObj, ::config.shade);
    if (shadeObj["colors"].is<JsonArray>()) {
      apply::shadeColorsToConfig(shadeObj["colors"].as<JsonArray>());
    }
  }
  // expressions[]: SKIPPED — see header comment. Per-entry
  // CHAR_EXPRESSION_OP is the canonical path.

  // Reboot opt-in. Default true if key omitted, for backward compat with
  // older apps.
  return doc["reboot"] | true;
}

}  // namespace apply
}  // namespace lamp
