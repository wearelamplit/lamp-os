// software/lamp-os/src/core/hw_config.hpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <lampos/led_types.hpp>

#include "components/input/input_source.hpp"

namespace lamp {

enum class Surface : uint8_t { Shade = 0, Base = 1 };

using input::InputType;

// One physical input (button or capacitive touch pad). `id` is the
// variant-chosen handle a behavior looks up to bind gestures. Touch fields
// are ignored for buttons.
struct InputSpec {
  uint8_t   id;
  InputType type;
  uint8_t   pin;
  uint16_t  touchPressDelta   = 12;
  uint16_t  touchReleaseDelta = 6;
  uint16_t  touchHoldMs       = 400;
};

using lampos::led::ByteOrder;
using lampos::led::byteOrderFromString;

struct StripSpec {
  Surface     role;
  uint8_t     pin;
  ByteOrder   byteOrder;
  uint16_t    pixelCount = 0;   // 0 = resolved from Config px at runtime
  const char* name       = nullptr;
  uint8_t     broadcast  = 0;   // 1 = representative strip for this role; default first if none set
  bool        reversed   = false;  // physical winding: true when pixel 0 is the far end (strip wound back-to-front)
};

struct HwConfig {
  std::vector<StripSpec> strips;
  std::vector<InputSpec> inputs;
  uint8_t maxBrightness = 200;
  uint16_t supplyBudgetMa = 2000;
};

// Returns true if the config is well-formed:
//   - at least one strip per role (Shade + Base)
//   - no duplicate pins
//   - Σ pixelCount per role ≤ 255
//   - at most one broadcast=1 strip
//   - supplyBudgetMa > 0 (a zero budget would govern every frame to black)
// pixelCount=0 entries are Config-resolved at runtime (core roles); the
// Σ guard catches variant-default overflows before buffers are sized.
// Used by Lamp::setup() as a fatal gate; malformed configs halt the
// lamp with a visible LED blink rather than silently mis-initing
// NeoPixel strips.
inline bool validateHwConfig(const HwConfig& hw) {
  if (hw.strips.empty()) return false;
  if (hw.supplyBudgetMa == 0) return false;
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
  // An input pin must not collide with another input pin OR any strip pin;
  // a shared line drives a NeoPixel and reads a gesture at once, corrupting
  // both. The strip cross-check is as load-bearing as the input↔input one.
  for (size_t i = 0; i < hw.inputs.size(); ++i) {
    const uint8_t pin = hw.inputs[i].pin;
    for (const StripSpec& s : hw.strips) {
      if (s.pin == pin) return false;
    }
    for (size_t j = i + 1; j < hw.inputs.size(); ++j) {
      if (hw.inputs[j].pin == pin) return false;
    }
  }
  return true;
}

}  // namespace lamp
