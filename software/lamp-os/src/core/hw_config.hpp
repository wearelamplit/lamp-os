// software/lamp-os/src/core/hw_config.hpp
#pragma once
#include <cstdint>
#include <vector>

namespace lamp {

enum class Surface : uint8_t { Shade = 0, Base = 1 };
enum class ByteOrder : uint8_t { GRBW = 0, GRB = 1, BGR = 2 };

struct StripSpec {
  Surface     role;
  uint8_t     pin;
  ByteOrder   byteOrder;
  uint16_t    pixelCount = 0;   // 0 = resolved from Config px at runtime
  const char* name       = nullptr;
  uint8_t     broadcast  = 0;   // 1 = representative strip for this role; default first if none set
};

struct HwConfig {
  std::vector<StripSpec> strips;
  uint8_t maxBrightness = 200;
};

// Returns true if the config is well-formed:
//   - at least one strip per role (Shade + Base)
//   - no duplicate pins
//   - Σ pixelCount per role ≤ 255
//   - at most one broadcast=1 strip
// pixelCount=0 entries are Config-resolved at runtime (core roles); the
// Σ guard catches variant-default overflows before buffers are sized.
// Used by Lamp::setup() as a fatal gate — malformed configs halt the
// lamp with a visible LED blink rather than silently mis-initing
// NeoPixel strips.
inline bool validateHwConfig(const HwConfig& hw) {
  if (hw.strips.empty()) return false;
  bool hasShade = false, hasBase = false;
  uint32_t shadePx = 0, basePx = 0;
  uint8_t broadcastCount = 0;
  for (size_t i = 0; i < hw.strips.size(); ++i) {
    const StripSpec& s = hw.strips[i];
    if (s.role == Surface::Shade) { hasShade = true; shadePx += s.pixelCount; }
    else if (s.role == Surface::Base) { hasBase = true; basePx += s.pixelCount; }
    if (s.broadcast) ++broadcastCount;
    for (size_t j = i + 1; j < hw.strips.size(); ++j) {
      if (s.pin == hw.strips[j].pin) return false;
    }
  }
  if (!hasShade || !hasBase) return false;
  if (shadePx > 255 || basePx > 255) return false;
  if (broadcastCount > 1) return false;
  return true;
}

}  // namespace lamp
