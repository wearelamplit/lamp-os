#pragma once

#include <cstdint>

#include "core/boot_history.hpp"

// Setup progress. See docs/dev/embedded-heap.md "Reading the boot history".
namespace lamp::setup_stage {

// Records that setup finished `stage` and feeds the loop watchdog, so each
// stage gets the setup timeout to itself. Loop task only.
void reach(boot_history::Stage stage);

#ifdef LAMP_DEBUG
// For the bench. Each of the next `boots` boots spins at `stage` until the
// watchdog resets it. Survives resets in RTC memory; a power cut disarms it.
void armBenchHang(boot_history::Stage stage, uint8_t boots);
#endif

}  // namespace lamp::setup_stage
