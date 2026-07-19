#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wifi {

// State retained for the scan UI in the app. The lamp NEVER associates to
// a home AP (presence-only mode), so CONNECTING/CONNECTED never fire. The
// only meaningful transitions are IDLE <-> SCANNING.
enum State { IDLE, SCANNING, FAILED };

struct ScanResult {
  std::string ssid;
  int8_t rssi;
  bool encrypted;
};

void begin();

State state();
std::string lastError();   // "scan" | ""; only ever set when a scan fails

void startScan();
std::vector<ScanResult> consumeScanResults();  // drains; used by the UI notify

using StateChangeCallback = void (*)();
void setStateChangeCallback(StateChangeCallback cb);

// Caller registers a getter for "is home-mode currently enabled in config".
// wifi.cpp uses it to gate periodic background scans. When home-mode is off
// there's no consumer for the scan results, so it saves the radio time
// (and avoids the boot-time scan failures that can strand the radio off
// LAMP_ESPNOW_CHANNEL). Returning false (or registering no
// getter at all) disables periodic scans entirely; on-demand scans via
// startScan() still work.
using HomeModeEnabledGetter = bool (*)();
void setHomeModeEnabledGetter(HomeModeEnabledGetter fn);

// Caller registers a getter for "is an OTA session currently in flight".
// wifi.cpp uses it to suppress periodic background scans during OTA. The
// scan hops the radio for ~5s and silently drops ESP-NOW unicast in BOTH
// directions while it runs (an OFFER retry can land while the lamp's
// ACCEPT/REQ can't get back to the wisp).
// On-demand scans (BLE op:scan) are NOT gated; user-driven scans still
// work even if a stalled OTA never reports completion.
using OtaInProgressGetter = bool (*)();
void setOtaInProgressGetter(OtaInProgressGetter fn);

void tick();

// Home-presence detection. The lamp periodically scans (when no BT client
// is connected; scans during BT sessions stress the shared radio) and
// caches the visible SSIDs. `homeSsidVisible(ssid)` returns true if a
// recent scan saw the given SSID. No association, no credentials, no
// password ever leaves the lamp.
bool homeSsidVisible(const std::string& ssid);

// softAP shares LAMP_ESPNOW_CHANNEL so an associating phone doesn't yank
// the radio off the grid channel.
bool startSoftAp(const std::string& name);
void stopSoftAp();

}  // namespace wifi
