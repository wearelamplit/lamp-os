#pragma once

#include <cstdint>

// Firmware identity advertised over the mesh (HELLO) and embedded into the
// signed firmware footer. Bump on every release that should be
// distinguishable to wisp's OTA picker.
//
// Layout: (major << 16) | (minor << 8) | patch. Stored as uint32_t for cheap
// `<` comparisons in the distributor's "lowest version peer" pick.

// Defaults for native env / sanity. Firmware envs override via build_flags
// (see [env:upesy_wroom] LAMP_FW_* defines).
#ifndef LAMP_FW_MAJOR
#define LAMP_FW_MAJOR 0
#endif
#ifndef LAMP_FW_MINOR
#define LAMP_FW_MINOR 0
#endif
#ifndef LAMP_FW_PATCH
#define LAMP_FW_PATCH 0
#endif

#define LAMP_STRINGIFY_(x) #x
#define LAMP_STRINGIFY(x)  LAMP_STRINGIFY_(x)

namespace lamp {

// Wire format / cheap < comparison. Derived from LAMP_FW_* at compile time.
constexpr uint32_t FIRMWARE_VERSION =
    (LAMP_FW_MAJOR << 16) | (LAMP_FW_MINOR << 8) | LAMP_FW_PATCH;

// Human-readable. Stringified via the preprocessor from the same LAMP_FW_*
// integers — single source of truth.
constexpr const char* FIRMWARE_VERSION_STR =
    LAMP_STRINGIFY(LAMP_FW_MAJOR) "."
    LAMP_STRINGIFY(LAMP_FW_MINOR) "."
    LAMP_STRINGIFY(LAMP_FW_PATCH);

// Channel comes from a -D flag baked at build time
// (`-D FIRMWARE_CHANNEL='"beta"'`) and gates cross-channel OTA offers
// (a stable lamp ignores a beta offer and vice versa). Default "stable"
// applies when no override is provided.
#ifndef FIRMWARE_CHANNEL
#define FIRMWARE_CHANNEL "stable"
#endif
constexpr const char* FIRMWARE_CHANNEL_STR = FIRMWARE_CHANNEL;

}  // namespace lamp
