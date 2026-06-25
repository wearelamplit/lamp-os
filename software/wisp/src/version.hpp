#pragma once

#include <cstdint>

// Firmware identity for the wisp (BLE-pushed OTA target). Same shape as the
// lamp's `lamp::FIRMWARE_VERSION` for consistency with `sign_firmware.py`'s
// version parser (looks for `FIRMWARE_VERSION = 0xMMmmpp` literal).
//
// Layout: (major << 16) | (minor << 8) | patch. Bump on every release that
// should be distinguishable to the app's "push if newer" check.
namespace wisp {

constexpr uint32_t FIRMWARE_VERSION = 0x000100;  // 0.1.0 (BLE-OTA bring-up)

}  // namespace wisp
