#pragma once

#if LAMP_WEBAPP_ENABLED

#include "config/config.hpp"

// Boot-window softAP-served fallback config for colors + name.
namespace webapp {

// Must be called after wifi::begin so the WiFi stack is initialised.
void begin(lamp::Config& config);
void tick();
bool isActive();

// Force the SoftAP + HTTP/WS server down NOW (vs. waiting for the
// 120 s boot-window deadline). Called from pauseRadioForOta() so the
// AP beacons (every ~100 ms) don't keep eating WiFi airtime while
// ESP-NOW OTA chunks need it. Idempotent — safe to call when already
// torn down. There's no companion "bring it back" because the webapp
// is first-boot config only; it doesn't come back for the rest of
// this boot regardless.
void shutdownForOta();

}  // namespace webapp

#endif  // LAMP_WEBAPP_ENABLED
