#include "CurrentPalette.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
// Native test build — stub the FreeRTOS surface to no-ops. Single-thread
// test harness has no concurrent access; the mutex acts solely as a
// sequence point on hardware, so dropping it for native is safe.
#include <cstddef>
typedef void* SemaphoreHandle_t;
#define pdTRUE         1
#define portMAX_DELAY  0xFFFFFFFFu
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  return reinterpret_cast<SemaphoreHandle_t>(0x1);
}
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wisp {

namespace {

inline SemaphoreHandle_t asHandle(void* m) {
  return reinterpret_cast<SemaphoreHandle_t>(m);
}

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

CurrentPalette::CurrentPalette()  { mux_ = xSemaphoreCreateMutex(); }
CurrentPalette::~CurrentPalette() { if (mux_) vSemaphoreDelete(asHandle(mux_)); }

void CurrentPalette::update(const Palette& p, uint32_t nowMs) {
  // paletteId_ assignment heap-allocates for non-SSO ids; running it inside
  // a spinlock (portENTER_CRITICAL) calls malloc with interrupts disabled and
  // starves the ESP-NOW ISR. A mutex lets the scheduler run during the alloc.
  xSemaphoreTake(asHandle(mux_), portMAX_DELAY);
  paletteId_ = p.id;
  xSemaphoreGive(asHandle(mux_));
  lastChangeMs_ = nowMs;
  colors_.clear();

  // hexColors is the simpler shape (24-bit packed RGB). When present we use
  // it directly with w=0; this is how the read-only built-in palettes arrive.
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

  // colors[] carries float channels. amber and uv are intentionally dropped —
  // wisp paints the lamp grid in RGBW and has no place for those channels.
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
  xSemaphoreTake(asHandle(mux_), portMAX_DELAY);
  n = paletteId_.size();
  if (n > outCap) n = outCap;
  if (n) std::memcpy(out, paletteId_.data(), n);
  xSemaphoreGive(asHandle(mux_));
  return n;
}

}  // namespace wisp
