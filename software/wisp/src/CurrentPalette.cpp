#include "CurrentPalette.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wisp {

namespace {

uint8_t floatToByte(float v) {
  // Clamp to [0,1] then scale. lroundf rounds half-away-from-zero, which is
  // close enough for visual data and avoids the down-bias of static_cast.
  if (v <= 0.0f) return 0;
  if (v >= 1.0f) return 255;
  long n = lroundf(v * 255.0f);
  if (n < 0) return 0;
  if (n > 255) return 255;
  return static_cast<uint8_t>(n);
}

}  // namespace

void CurrentPalette::update(const Palette& p, uint32_t nowMs) {
  CURRENT_PALETTE_PORTMUX_ENTER(&mux_);
  paletteId_ = p.id;
  CURRENT_PALETTE_PORTMUX_EXIT(&mux_);
  lastChangeMs_ = nowMs;
  colors_.clear();

  // hexColors: 24-bit packed RGB, used directly with w=0 (built-in palettes).
  if (!p.hexColors.empty()) {
    colors_.reserve(p.hexColors.size());
    for (uint64_t hex : p.hexColors) {
      const uint32_t v = static_cast<uint32_t>(hex);
      RGBW c;
      c.r = static_cast<uint8_t>((v >> 16) & 0xFF);
      c.g = static_cast<uint8_t>((v >> 8) & 0xFF);
      c.b = static_cast<uint8_t>(v & 0xFF);
      c.w = 0;
      colors_.push_back(c);
    }
    return;
  }

  // colors[] float channels; amber and uv dropped.
  colors_.reserve(p.colors.size());
  for (const auto& src : p.colors) {
    RGBW c;
    c.r = floatToByte(src.r);
    c.g = floatToByte(src.g);
    c.b = floatToByte(src.b);
    c.w = floatToByte(src.w);
    colors_.push_back(c);
  }
}

size_t CurrentPalette::copyPaletteIdPrefix(char* out, size_t outCap) const {
  if (!out || outCap == 0) return 0;
  size_t n = 0;
  CURRENT_PALETTE_PORTMUX_ENTER(&mux_);
  n = paletteId_.size();
  if (n > outCap) n = outCap;
  if (n) std::memcpy(out, paletteId_.data(), n);
  CURRENT_PALETTE_PORTMUX_EXIT(&mux_);
  return n;
}

}  // namespace wisp
