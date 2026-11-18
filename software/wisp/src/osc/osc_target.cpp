#include "osc/osc_target.hpp"

#include <cstdio>
#include <cstring>

namespace wisp {

namespace {

constexpr const char* kOscModeNames[] = {"straylight", "maestro"};
constexpr size_t kOscModeCount = sizeof(kOscModeNames) / sizeof(kOscModeNames[0]);

// A Maestro target reads `/palette/live/<slot>`.
constexpr char kMaestroAddressPrefix[] = "/palette/live/";

bool resets(const OscTarget& from, const OscTarget& to) {
  if (from.mode != to.mode) return true;
  return to.mode == OscMode::Maestro && from.slot != to.slot;
}

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
    case OscMode::Straylight:
      break;
  }
  std::snprintf(out, cap, "%s", oscPaletteAddress(target.role));
}

uint32_t oscTargetStaleMs(const OscTarget& target) {
  return target.mode == OscMode::Straylight ? OscFreshness::kStaleMs : 0;
}

void OscStreamGate::seed(const OscTarget& target) {
  target_ = target;
  fresh_.setStaleMs(oscTargetStaleMs(target));
}

bool OscStreamGate::apply(const OscTarget& target, bool sourceIsOsc) {
  const bool hold = sourceIsOsc && resets(target_, target);
  seed(target);
  return hold;
}

}  // namespace wisp
