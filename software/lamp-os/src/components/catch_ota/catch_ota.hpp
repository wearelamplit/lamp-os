#pragma once

#include <cstdint>

namespace catch_ota {

// name + base/shade RGBW are advertised in the lamp's mesh HELLO (real identity
// while it waits to be upgraded).
void begin(const char* name, const uint8_t baseRGBW[4],
           const uint8_t shadeRGBW[4]);
void tick(uint32_t nowMs);
bool isInProgress();

}  // namespace catch_ota
