// software/lamp-os/src/core/hw_config.hpp
#pragma once
#include <cstdint>
#include <vector>

namespace lamp {

enum class Surface : uint8_t { Shade = 0, Base = 1 };
enum class ByteOrder : uint8_t { GRBW = 0, GRB = 1, BGR = 2 };

struct SurfaceSpec {
  Surface id;
  uint8_t pin;
  ByteOrder byteOrder;
};

struct HwConfig {
  std::vector<SurfaceSpec> surfaces;
  uint8_t maxBrightness = 200;
};

// Returns true if the config is well-formed:
//   - at least one surface
//   - no duplicate pins across surfaces
// Pixel counts live in Config (NVS-loaded, app-editable) — the variant
// seeds first-boot values via Config::Defaults::{base,shade}Px.
// Used by Lamp::setup() as a fatal gate — malformed configs halt the
// lamp with a visible LED blink rather than silently mis-initing
// NeoPixel strips.
inline bool validateHwConfig(const HwConfig& hw) {
  if (hw.surfaces.empty()) return false;
  for (size_t i = 0; i < hw.surfaces.size(); ++i) {
    for (size_t j = i + 1; j < hw.surfaces.size(); ++j) {
      if (hw.surfaces[i].pin == hw.surfaces[j].pin) return false;
    }
  }
  return true;
}

}  // namespace lamp
