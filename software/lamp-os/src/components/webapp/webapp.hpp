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

// Mount point of the web UI files (the SPIFFS partition, unlabelled). The web
// server mounts it there while it serves; fs_ota mounts it there for a digest
// pass and reuses the server's mount when one is up.
constexpr char kUiFsBasePath[] = "/spiffs";

// Call after the web UI partition is rewritten in place, on the loop task. A
// web server that is up gets a fresh mount of the new files; otherwise no-op.
void remountUiFiles();

// Takes the softAP down for an OTA radio pause, so its beacons leave the
// airtime to the chunk stream. ota_quiet_mode calls it after each pause,
// outside its transition lock, on the pausing task. Waits for any AP work a
// tick() pass has in progress. tick() brings the AP back once the pause ends
// if this took it down and the webapp would still keep it up.
void shutdownForOta();

}  // namespace webapp

#endif  // LAMP_WEBAPP_ENABLED
