// settings_blob's `lamp` section handler. Updates config.lamp.* fields
// in place, calls NimBLEDevice::setDeviceName on rename so the BLE
// advertised name reflects the new value without a reboot. Mesh HELLO
// (mesh_link.cpp) reads config.lamp.name live every 5s so peers
// see the rename on the next tick.

#pragma once

#include <ArduinoJson.h>

#include "config/config.hpp"
#include "components/apply/apply_brightness.hpp"

// config is defined as `lamp::Config config;` at file scope in
// lamp.cpp; it lives at ::config, not ::lamp::config.
extern lamp::Config config;

namespace lamp {

// Provided by lamp.cpp / NimBLE wiring. Call to update the
// GAP device name advertised in scan responses without rebooting.
// Implementation calls NimBLEDevice::setDeviceName(newName).
void updateAdvertisedDeviceName(const char* newName);

namespace apply {

// Applies all writable fields in the `lamp` JSON object to config and
// to runtime state. Missing fields are left alone (settings_blob is
// partial-merge by design; caller omits what it doesn't want to touch).
inline void lampLocal(JsonObject obj, uint8_t maxBrightness) {
  if (obj.isNull()) return;
  if (obj["name"].is<const char*>()) {
    ::config.lamp.name = obj["name"].as<const char*>();
    ::lamp::updateAdvertisedDeviceName(::config.lamp.name.c_str());
  }
  if (obj["brightness"].is<int>()) {
    int level = obj["brightness"].as<int>();
    if (level >= 0 && level <= 100) {
      // Settings_blob is a saved value; use brightnessImmediate to skip the slider micro-fade.
      ::lamp::apply::brightnessImmediate(static_cast<uint8_t>(level),
                                          /*isHomeMode=*/false, maxBrightness);
    }
  }
  // Set once at adoption (the claim blob carries "setup": true). The app
  // never clears it; a factory reset is what returns a lamp to unconfigured.
  const bool wasSetup = ::config.lamp.setup;
  if (obj["setup"].is<bool>()) {
    ::config.lamp.setup = obj["setup"].as<bool>();
  }
  // First adoption stamps a finite AP window; the factory default is forever.
  // Skip the stamp when the blob carries an explicit apBootMinutes, so the
  // value applied on the persisted-config reload is preserved.
  if (!wasSetup && ::config.lamp.setup && !obj["apBootMinutes"].is<int>()) {
    ::config.lamp.apBootMinutes = 2;
  }
  if (obj["advancedEnabled"].is<bool>()) {
    ::config.lamp.advancedEnabled = obj["advancedEnabled"].as<bool>();
  }
  if (obj["webappEnabled"].is<bool>()) {
    ::config.lamp.webappEnabled = obj["webappEnabled"].as<bool>();
  }
  if (obj["socialMode"].is<int>()) {
    int mode = obj["socialMode"].as<int>();
    if (mode >= 0 && mode <= 2) {  // Introvert / Ambivert / Extrovert
      ::config.lamp.socialMode = static_cast<SocialMode>(mode);
    }
  }
  if (obj["brightnessCeiling"].is<int>()) {
    int ceiling = obj["brightnessCeiling"].as<int>();
    if (ceiling >= 1 && ceiling <= 255) {
      ::config.lamp.brightnessCeiling = static_cast<uint8_t>(ceiling);
    }
  }
}

}  // namespace apply
}  // namespace lamp
