// Applies a role section's strip format (per-segment pixel counts and data
// pins + role-level byteOrder) from an incoming settings_blob. A segment pin
// of 0 restores the variant pin; any other value must pass isStripDataPin,
// and pin clashes are settled at boot by resolveStripPins. px is positional
// against the role's existing segments; extra entries are ignored (segment
// count is variant-fixed, not app-editable). byteOrder is a whole-role
// string. Absent keys leave the current value. Takes effect on the reboot
// the blob triggers, where fromJson re-clamps Σ≤255 and the strips rebuild.
// No live re-init here.
//
// Templated on the surface type so the one body serves both BaseSettings
// and ShadeSettings (and lets the native test exercise the real code).

#pragma once

#include <ArduinoJson.h>

#include <cstdint>

#include <lampos/led_types.hpp>

#include "core/hw_config.hpp"

namespace lamp {
namespace apply {

template <typename Surface>
inline void pixelFormatLocal(JsonObjectConst section, Surface& surf) {
  // An order the strip driver does not know is ignored, which also bounds
  // the stored field.
  lampos::led::ByteOrder order;
  if (section["byteOrder"].is<const char*>() &&
      lampos::led::byteOrderFromString(section["byteOrder"].as<const char*>(), order)) {
    surf.byteOrder = section["byteOrder"].as<const char*>();
  }
  JsonArrayConst segs = section["segments"];
  if (segs.isNull()) return;
  size_t k = 0;
  for (JsonObjectConst seg : segs) {
    if (k >= surf.segments.size()) break;
    if (seg["px"].is<int>()) {
      int px = seg["px"].as<int>();
      if (px >= 1 && px <= 255) surf.segments[k].px = static_cast<uint8_t>(px);
    }
    if (seg["pin"].is<int>()) {
      int pin = seg["pin"].as<int>();
      if (pin == 0 || (pin > 0 && pin <= 255 && isStripDataPin(static_cast<uint8_t>(pin)))) {
        surf.segments[k].pin = static_cast<uint8_t>(pin);
      }
    }
    ++k;
  }
}

}  // namespace apply
}  // namespace lamp
