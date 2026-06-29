#pragma once

#include <cstdint>

namespace catch_ota {

void begin();
void tick(uint32_t nowMs);
bool isInProgress();

}  // namespace catch_ota
