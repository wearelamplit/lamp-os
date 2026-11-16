#pragma once

#if LAMP_WEBAPP_ENABLED

#include "config/config.hpp"

// Boot-window softAP-served fallback config for colors + name.
namespace webapp {

// Must be called after wifi::begin so the WiFi stack is initialised.
void begin(lamp::Config& config);
void tick();
bool isActive();

// True while a station is joined to the config softAP (editor open), so the
// AP is actively transmitting rather than just beaconing.
bool hasClient();

// Takes the softAP down for an OTA radio pause, so its beacons leave the
// airtime to the chunk stream. ota_quiet_mode calls it after each pause,
// outside its transition lock, on the pausing task. tick() brings the AP
// back once the pause ends if this took it down and the webapp would still
// keep it up.
void shutdownForOta();

}  // namespace webapp

#endif  // LAMP_WEBAPP_ENABLED
