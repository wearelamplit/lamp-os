// software/lamp-os/src/components/apply/apply_pixel_format.hpp
//
// Applies a surface's strip format (px / bpp / byteOrder) from an incoming
// settings_blob section. Only keys actually present in the blob are
// touched; absent keys leave the current value. These take effect on the
// reboot the settings_blob triggers — the strip is rebuilt from them at
// boot, so there's no live re-init here.
//
// Templated on the surface type so the one body serves both BaseSettings
// and ShadeSettings (and lets the native test exercise the real code).

#pragma once

#include <ArduinoJson.h>

#include <cstdint>

namespace lamp {
namespace apply {

template <typename Surface>
inline void pixelFormatLocal(JsonObjectConst section, Surface& surf) {
  if (section["px"].is<int>()) {
    int px = section["px"].as<int>();
    if (px > 50) px = 50;  // clamp to the loader's bound (config.cpp)
    if (px >= 1) surf.px = static_cast<uint8_t>(px);  // ignore nonsense <= 0
  }
  if (section["bpp"].is<int>()) {
    int bpp = section["bpp"].as<int>();
    if (bpp == 3 || bpp == 4) surf.bpp = static_cast<uint8_t>(bpp);
  }
  if (section["byteOrder"].is<const char*>()) {
    surf.byteOrder = section["byteOrder"].as<const char*>();
  }
}

}  // namespace apply
}  // namespace lamp
