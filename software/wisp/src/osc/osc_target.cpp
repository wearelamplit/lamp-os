#include "osc/osc_target.hpp"

#include "osc/osc_freshness.hpp"

#include <cstdio>
#include <cstring>

namespace wisp {

namespace {

constexpr const char* kOscModeNames[] = {"straylight", "maestro", "custom"};
constexpr size_t kOscModeCount = sizeof(kOscModeNames) / sizeof(kOscModeNames[0]);

// A Maestro target reads `/palette/live/<slot>`.
constexpr char kMaestroAddressPrefix[] = "/palette/live/";

// OSC pattern syntax, the JSON escapes, and the bundle and tag markers.
constexpr char kCustomAddressForbidden[] = "\"\\#*,?[]{}";

}  // namespace

OscMode coerceOscMode(int raw) {
  return raw >= 0 && static_cast<size_t>(raw) < kOscModeCount
             ? static_cast<OscMode>(raw)
             : OscMode::Straylight;
}

bool maestroSlotValid(int slot) {
  return slot >= kMaestroSlotMin && slot <= kMaestroSlotMax;
}

uint8_t coerceMaestroSlot(int raw) {
  return maestroSlotValid(raw) ? static_cast<uint8_t>(raw) : kMaestroSlotMin;
}

bool maestroSlotFromText(const char* text, uint8_t& out) {
  if (!text || !*text) return false;
  int slot = 0;
  for (const char* c = text; *c; ++c) {
    if (*c < '0' || *c > '9' || slot > kMaestroSlotMax) return false;
    slot = slot * 10 + (*c - '0');
  }
  if (!maestroSlotValid(slot)) return false;
  out = static_cast<uint8_t>(slot);
  return true;
}

bool oscCustomAddressValid(const char* address) {
  if (!address || address[0] != '/') return false;
  size_t len = 0;
  for (const char* c = address; *c; ++c) {
    if (++len > kOscCustomAddressMaxLen) return false;
    if (*c < 0x21 || *c > 0x7E) return false;
    if (std::strchr(kCustomAddressForbidden, *c)) return false;
  }
  return true;
}

const char* oscModeName(OscMode mode) {
  return kOscModeNames[static_cast<uint8_t>(
      coerceOscMode(static_cast<int>(mode)))];
}

bool oscModeFromName(const char* name, OscMode& out) {
  if (!name) return false;
  for (size_t i = 0; i < kOscModeCount; ++i) {
    if (std::strcmp(name, kOscModeNames[i]) == 0) {
      out = static_cast<OscMode>(i);
      return true;
    }
  }
  return false;
}

void oscTargetAddress(const OscTarget& target, char* out, size_t cap) {
  switch (target.mode) {
    case OscMode::Maestro:
      std::snprintf(out, cap, "%s%u", kMaestroAddressPrefix,
                    static_cast<unsigned>(coerceMaestroSlot(target.slot)));
      return;
    case OscMode::Custom:
      std::snprintf(out, cap, "%s", target.address);
      return;
    case OscMode::Straylight:
      break;
  }
  std::snprintf(out, cap, "%s", oscPaletteAddress(target.role));
}

uint32_t oscTargetStaleMs(const OscTarget& target) {
  return target.mode == OscMode::Straylight ? OscFreshness::kStaleMs : 0;
}

bool oscTargetResets(const OscTarget& from, const OscTarget& to) {
  if (from.mode != to.mode) return true;
  switch (to.mode) {
    case OscMode::Maestro:
      return from.slot != to.slot;
    case OscMode::Custom:
      return std::strcmp(from.address, to.address) != 0;
    case OscMode::Straylight:
      break;
  }
  return false;
}

}  // namespace wisp
