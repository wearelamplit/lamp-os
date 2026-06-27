// CurrentPalette — the most recently resolved Aurora palette for the zone wisp
// is shadowing. PaintDistributor pipes these colors to the lamp grid as
// the painting source.
//
// Storage is uint8_t RGBW. Aurora carries two color shapes:
//   - hexColors: 24-bit packed RGB (w=0).
//   - colors[]:  float channels 0..1 (r,g,b,w + amber/uv we ignore).
// The amber + uv channels are dropped — wisp's downstream consumers are the
// lamp grid (RGBW), so chromatic remap of those would be lossy and confusing.
//
// Header is host-portable (no Arduino includes). The .cpp uses millis() at the
// call site, not here.
//
// THREADING: update() runs on the loop task; the read accessors paletteId() /
// colors() are loop-task only. copyPaletteIdPrefix() is mutex-guarded and can
// be called from the FreeRTOS timer-service task (StatusBeacon::emit /
// emitStatus). A mutex (not a spinlock) is required because paletteId_
// assignment heap-allocates and malloc cannot run with interrupts disabled.
// The .cpp is excluded from the native build because the mutex is FreeRTOS-only.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "aurora/PaletteList.h"  // Palette, PaletteColor

namespace wisp {

struct RGBW {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t w = 0;
};

class CurrentPalette {
 public:
  CurrentPalette();
  ~CurrentPalette();

  // Replace the held palette with the resolved Aurora colors. `nowMs` is the
  // wall-clock at the time of replacement (caller supplies millis() so the
  // header can stay framework-free).
  void update(const Palette& p, uint32_t nowMs);

  const std::string& paletteId() const { return paletteId_; }
  uint32_t lastChangeMs() const { return lastChangeMs_; }
  const std::vector<RGBW>& colors() const { return colors_; }

  // Snapshot first up-to-`outCap` chars of paletteId into `out` (no null
  // terminator written). Returns the number of chars written. Thread-safe —
  // takes the internal mux, so this is the only accessor safe to call from
  // the FreeRTOS timer-service task while update() is racing on the loop
  // task.
  size_t copyPaletteIdPrefix(char* out, size_t outCap) const;

 private:
  std::string paletteId_;
  uint32_t lastChangeMs_ = 0;
  std::vector<RGBW> colors_;

  // Mutex handle — opaque to keep FreeRTOS out of the header. Cast back to
  // SemaphoreHandle_t in the .cpp. Same pattern as WispRoster / WispConfig.
  // colors_ and lastChangeMs_ are NOT guarded — only the loop task touches those.
  void* mux_ = nullptr;
};

}  // namespace wisp
