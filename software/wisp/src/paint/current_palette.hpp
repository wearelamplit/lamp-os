// CurrentPalette — most recently resolved Aurora palette for the shadowed zone.
//
// Aurora's amber channel folds into the warm-white W channel (the grid's
// dedicated warm emitter).
// update() and paletteId()/colors() are loop-task only. copyPaletteIdPrefix()
// is mutex-guarded for timer-task callers. A mutex (not spinlock) is required
// because paletteId_ assignment heap-allocates; malloc cannot run with IRQs off.

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

  void clear();

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

  // Opaque to keep FreeRTOS out of the header; cast to SemaphoreHandle_t in .cpp.
  // colors_ and lastChangeMs_ are loop-task only; not guarded.
  void* mux_ = nullptr;
};

}  // namespace wisp
