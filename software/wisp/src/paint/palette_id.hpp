#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "config/manual_palette_color.hpp"
#include "util/fnv1a.hpp"

namespace wisp {

// Content hash over the palette's canonical wire bytes: r,g,b per color, plus
// w only when w != 0. Matches the app's WispRepository.paletteIdHash so a
// palette write is confirmed on the reliable status NOTIFY. 8 lowercase hex
// chars, exactly WISP_HELLO_PALETTE_ID_PREFIX_LEN.
inline std::string manualPaletteId(const std::vector<ManualPaletteColor>& cols) {
  std::vector<uint8_t> wire;
  wire.reserve(cols.size() * 4);
  for (const auto& c : cols) {
    wire.push_back(c.r);
    wire.push_back(c.g);
    wire.push_back(c.b);
    if (c.w != 0) wire.push_back(c.w);
  }
  char buf[9];
  std::snprintf(buf, sizeof(buf), "%08x", fnv1a(wire.data(), wire.size()));
  return std::string(buf);
}

}  // namespace wisp
