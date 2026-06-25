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
// colors() are loop-task only. copyPaletteIdPrefix() is portMUX-guarded and
// can be called from the FreeRTOS timer-service task (StatusBeacon::emit /
// emitStatus). The .cpp is excluded from the native build because the mux is
// FreeRTOS-only.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <portmacro.h>
#define CURRENT_PALETTE_PORTMUX_TYPE       portMUX_TYPE
#define CURRENT_PALETTE_PORTMUX_INIT       portMUX_INITIALIZER_UNLOCKED
#define CURRENT_PALETTE_PORTMUX_ENTER(mux) portENTER_CRITICAL(mux)
#define CURRENT_PALETTE_PORTMUX_EXIT(mux)  portEXIT_CRITICAL(mux)
#else
struct CurrentPaletteNullMux {};
#define CURRENT_PALETTE_PORTMUX_TYPE       CurrentPaletteNullMux
#define CURRENT_PALETTE_PORTMUX_INIT       {}
#define CURRENT_PALETTE_PORTMUX_ENTER(mux) ((void)(mux))
#define CURRENT_PALETTE_PORTMUX_EXIT(mux)  ((void)(mux))
#endif

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

  // Guards paletteId_ so timer-task snapshots don't tear against a loop-task
  // update(). colors_ and lastChangeMs_ are intentionally NOT guarded — only
  // the loop task touches those today.
  mutable CURRENT_PALETTE_PORTMUX_TYPE mux_ = CURRENT_PALETTE_PORTMUX_INIT;
};

}  // namespace wisp
