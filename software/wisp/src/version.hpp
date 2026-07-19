#pragma once

#include <cstdint>

// Firmware identity for the wisp (BLE-pushed OTA target). Version is the single
// source in the root VERSION file, injected as LAMP_FW_* by inject_version.py
// and shared with the lamp.
//
// Layout: (major << 16) | (minor << 8) | patch. Bump on every release that
// should be distinguishable to the app's "push if newer" check.

// Defaults for native env / sanity. Firmware builds override via injected
// LAMP_FW_* defines.
#ifndef LAMP_FW_MAJOR
#define LAMP_FW_MAJOR 0
#endif
#ifndef LAMP_FW_MINOR
#define LAMP_FW_MINOR 0
#endif
#ifndef LAMP_FW_PATCH
#define LAMP_FW_PATCH 0
#endif

namespace wisp {

constexpr uint32_t FIRMWARE_VERSION =
    (LAMP_FW_MAJOR << 16) | (LAMP_FW_MINOR << 8) | LAMP_FW_PATCH;

}  // namespace wisp
