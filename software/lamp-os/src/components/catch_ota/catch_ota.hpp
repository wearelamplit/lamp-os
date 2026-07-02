#pragma once

#include <cstdint>

namespace lamp {
class Color;
}

namespace catch_ota {

// name + base/shade color are advertised in the lamp's mesh HELLO (real identity
// while it waits to be upgraded).
void begin(const char* name, const lamp::Color& base, const lamp::Color& shade);
void tick(uint32_t nowMs);
bool isInProgress();

}  // namespace catch_ota
