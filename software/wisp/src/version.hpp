#pragma once

#include <cstdint>

// sign_firmware.py expects the literal `FIRMWARE_VERSION = 0xMMmmpp`.
// Layout: (major << 16) | (minor << 8) | patch. Bump on every release that
// should be distinguishable to the app's "push if newer" check.
namespace wisp {

constexpr uint32_t FIRMWARE_VERSION = 0x000100;

}  // namespace wisp
