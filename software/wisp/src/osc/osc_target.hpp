// What the wisp listens to in Osc mode (the sub-mode and its role or slot),
// and when changing it resets the stream. Loop task only.

#pragma once

#include <cstddef>
#include <cstdint>

#include "osc/osc_freshness.hpp"
#include "osc/osc_role.hpp"

namespace wisp {

// NVS u8 `oscMode`. The names are the wire strings for wispOp setOsc, the
// console and wispConfig.
enum class OscMode : uint8_t {
  Straylight = 0,
  Maestro    = 1,
};

inline constexpr uint8_t kMaestroSlotMin = 1;
inline constexpr uint8_t kMaestroSlotMax = 10;

// Unknown raw values (a corrupted NVS byte) read as Straylight.
OscMode coerceOscMode(int raw);
// A slot outside kMaestroSlotMin..kMaestroSlotMax reads as kMaestroSlotMin.
uint8_t coerceMaestroSlot(int raw);
bool maestroSlotValid(int slot);

const char* oscModeName(OscMode mode);
// False when `name` is null or not a mode name.
bool oscModeFromName(const char* name, OscMode& out);

struct OscTarget {
  OscMode mode = OscMode::Straylight;
  OscRole role = OscRole::Primary;  // read in Straylight only
  uint8_t slot = kMaestroSlotMin;   // read in Maestro only
};

// Fits every address oscTargetAddress writes, NUL included.
inline constexpr size_t kOscTargetAddressCap = 40;

// The one address `target` paints from, matched exactly.
void oscTargetAddress(const OscTarget& target, char* out, size_t cap);

// How long a live stream lasts with nothing from its sender; 0 never stales.
uint32_t oscTargetStaleMs(const OscTarget& target);

// Owns the stream's liveness and decides when a new target resets it.
class OscStreamGate {
 public:
  // Boot: records `target` and its stale window. Never holds the stream.
  void seed(const OscTarget& target);

  // A target an op or the console stored. True when the caller must hold
  // lamps at Off until the new target's first palette: the source is Osc
  // and the target changed other than by a Straylight role. Records
  // `target` and its stale window either way.
  bool apply(const OscTarget& target, bool sourceIsOsc);

  OscFreshness& freshness() { return fresh_; }
  const OscFreshness& freshness() const { return fresh_; }

 private:
  OscTarget target_;
  OscFreshness fresh_;
};

}  // namespace wisp
