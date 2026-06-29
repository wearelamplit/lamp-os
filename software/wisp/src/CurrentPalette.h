// CurrentPalette — most recently resolved Aurora palette for the zone the
// wisp shadows. PaintDistributor pipes these colors to the lamp grid.
//
// Storage is uint8_t RGBW. Aurora's amber + uv channels are dropped (the lamp
// grid is RGBW; remapping them would be lossy). Host-portable header.
//
// update() and the read accessors are loop-task only; only paletteId_ is
// mux-guarded (colors_/lastChangeMs_ are unguarded). copyPaletteIdPrefix() is
// the only accessor safe from the timer-service task. The .cpp is excluded
// from the native build (the mux is FreeRTOS-only).

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

  // Snapshot up to outCap chars of paletteId into out (no null terminator).
  // Returns chars written. Takes the internal mux.
  size_t copyPaletteIdPrefix(char* out, size_t outCap) const;

 private:
  std::string paletteId_;
  uint32_t lastChangeMs_ = 0;
  std::vector<RGBW> colors_;

  mutable CURRENT_PALETTE_PORTMUX_TYPE mux_ = CURRENT_PALETTE_PORTMUX_INIT;
};

}  // namespace wisp
