#pragma once

#include <cstdint>

namespace wisp {

// How lamps take colours from the palette. Gradient samples a blend of it;
// Swatches hands each lamp palette entries exactly. NVS key `swatches` stores
// the underlying value.
enum class PaletteStyle : uint8_t { Gradient = 0, Swatches = 1 };

}  // namespace wisp
