// What the wisp listens to in Osc mode (the sub-mode and its role or slot),
// and when changing it resets the stream.

#pragma once

#include <cstddef>
#include <cstdint>

#include "osc/osc_role.hpp"

namespace wisp {

// NVS u8 `oscMode`. The names are the wire strings for wispOp setOsc, the
// console and wispConfig.
enum class OscMode : uint8_t {
  Straylight = 0,
  Maestro    = 1,
  Custom     = 2,
};

inline constexpr uint8_t kMaestroSlotMin = 1;
inline constexpr uint8_t kMaestroSlotMax = 10;

// Unknown raw values (a corrupted NVS byte) read as Straylight.
OscMode coerceOscMode(int raw);
// A slot outside kMaestroSlotMin..kMaestroSlotMax reads as kMaestroSlotMin.
uint8_t coerceMaestroSlot(int raw);
bool maestroSlotValid(int slot);
// A console slot argument: decimal digits only, in range. False otherwise.
bool maestroSlotFromText(const char* text, uint8_t& out);

// The longest Custom address. wispConfig carries it and must stay at most
// 255 B; one byte more breaks that pin.
inline constexpr size_t kOscCustomAddressMaxLen = 15;

// A Custom address: printable ASCII (0x21..0x7E), 1..kOscCustomAddressMaxLen
// bytes, starting with '/', none of `"` `\` `#` `*` `,` `?` `[` `]` `{` `}`.
// Such an address serializes into JSON unescaped. False for null.
bool oscCustomAddressValid(const char* address);

const char* oscModeName(OscMode mode);
// False when `name` is null or not a mode name.
bool oscModeFromName(const char* name, OscMode& out);

struct OscTarget {
  OscMode mode = OscMode::Straylight;
  OscRole role = OscRole::Primary;  // read in Straylight only
  uint8_t slot = kMaestroSlotMin;   // read in Maestro only
  // Read in Custom only: "" or valid per oscCustomAddressValid.
  char address[kOscCustomAddressMaxLen + 1] = "";
};

// Fits every address oscTargetAddress writes, NUL included.
inline constexpr size_t kOscTargetAddressCap = 40;

// The one address `target` paints from, matched exactly.
void oscTargetAddress(const OscTarget& target, char* out, size_t cap);

// How long a live stream lasts with nothing from its sender; 0 never stales.
uint32_t oscTargetStaleMs(const OscTarget& target);

// True when moving from `from` to `to` points the listener at another
// address, so a live stream must hold at Off until the new one's first
// palette. A Straylight role change keeps the old role painting instead.
bool oscTargetResets(const OscTarget& from, const OscTarget& to);

}  // namespace wisp
