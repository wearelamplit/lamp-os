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

// True for an ESP32-WROOM GPIO that can drive a strip's data line. Excludes
// GPIO 0 (boot mode), 1 and 3 (the UART0 console), 6-11 (the SPI flash bus),
// 34-39 (input-only), and numbers with no pad.
inline bool isStripDataPin(int pin) {
  switch (pin) {
    case 2: case 4: case 5:
    case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19:
    case 21: case 22: case 23: case 25: case 26: case 27: case 32: case 33:
      return true;
    default:
      return false;
  }
}

// Effective data pin per hw.strips entry. `requested[i]` (0 = none) replaces
// strips[i].pin when it is a strip data pin no other strip or input ends up
// on; anything else keeps the StripSpec pin, so a bad saved value can't stop
// a lamp from booting.
inline std::vector<uint8_t> resolveStripPins(const HwConfig& hw,
                                             const std::vector<uint8_t>& requested) {
  std::vector<uint8_t> pins(hw.strips.size());
  for (size_t i = 0; i < pins.size(); ++i) {
    const uint8_t want = i < requested.size() ? requested[i] : 0;
    pins[i] = isStripDataPin(want) ? want : hw.strips[i].pin;
  }
  // Every override in a clash reverts together, so no strip wins a contested
  // pin. A revert can only collide with another override (StripSpec pins are
  // distinct), so repeat until stable.
  bool changed = true;
  while (changed) {
    changed = false;
    std::vector<bool> revert(pins.size(), false);
    for (size_t i = 0; i < pins.size(); ++i) {
      if (pins[i] == hw.strips[i].pin) continue;
      for (size_t j = 0; j < pins.size(); ++j) {
        if (j != i && pins[j] == pins[i]) revert[i] = true;
      }
      for (const InputSpec& in : hw.inputs) {
        if (in.pin == pins[i]) revert[i] = true;
      }
    }
    for (size_t i = 0; i < pins.size(); ++i) {
      if (revert[i]) {
        pins[i] = hw.strips[i].pin;
        changed = true;
      }
    }
  }
  return pins;
}

// Effective data pin per hw.strips entry from the role segments' saved pins.
// A role's segments pair with its strips in declaration order. A saved pin
// that does not take effect, or sits on a segment with no strip, is cleared,
// so every segment reports the pin in use.
template <typename Seg>
std::vector<uint8_t> applySegmentPins(const HwConfig& hw, std::vector<Seg>& shade,
                                      std::vector<Seg>& base) {
  std::vector<Seg*> paired(hw.strips.size(), nullptr);
  std::vector<uint8_t> requested(hw.strips.size(), 0);
  size_t shadeK = 0, baseK = 0;
  for (size_t i = 0; i < hw.strips.size(); ++i) {
    const bool isShade = hw.strips[i].role == Surface::Shade;
    std::vector<Seg>& segs = isShade ? shade : base;
    const size_t k = isShade ? shadeK++ : baseK++;
    if (k < segs.size()) {
      paired[i] = &segs[k];
      requested[i] = segs[k].pin;
    }
  }
  for (size_t k = shadeK; k < shade.size(); ++k) shade[k].pin = 0;
  for (size_t k = baseK; k < base.size(); ++k) base[k].pin = 0;
  const std::vector<uint8_t> pins = resolveStripPins(hw, requested);
  for (size_t i = 0; i < pins.size(); ++i) {
    if (paired[i] && paired[i]->pin != pins[i]) paired[i]->pin = 0;
  }
  return pins;
}

}  // namespace lamp
