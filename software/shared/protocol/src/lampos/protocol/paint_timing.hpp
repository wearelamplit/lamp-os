#pragma once

// Wisp paint timing the lamp's ColorOverride watchdog holds to.

#include <cstdint>

namespace lamp_protocol {

// Lamp colour-override restore failsafe (ColorOverride::tick). Reverts a
// surface if no paint lands within this window of the wisp going silent.
constexpr uint32_t kPaintWatchdogMs = 100000;

}  // namespace lamp_protocol
