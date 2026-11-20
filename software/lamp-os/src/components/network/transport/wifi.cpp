#include "wifi.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <portmacro.h>

#include "components/network/ble/ble_control.hpp"  // for ble_control::isClientConnected()
#include "components/network/transport/ssid_presence.hpp"
#include "components/network/transport/wifi_psk.hpp"
#include "util/uptime.hpp"

// Grid channel. All unconnected grid lamps line up here so ESP-NOW peers
// can hear each other. The radio never associates to a home AP,
// so it sits on this channel permanently (modulo brief scan windows).
#ifndef LAMP_ESPNOW_CHANNEL
// Channel 6: the channel pre-mesh lamps hardcode for their ArtNet WiFi, so
// new mesh lamps, the wisp stage softAP, and old lamps all share one channel.
// Mirror of mesh_link.hpp and the wisp's mesh_link.hpp; a mixed-channel fleet
// does NOT interoperate (peers on another channel are invisible).
#define LAMP_ESPNOW_CHANNEL 6
#endif

namespace wifi {

static State s_state = IDLE;
static std::string s_lastError;
static std::vector<ScanResult> s_scanResults;  // an app-requested scan's list
static SsidPresence s_presence;                // the latest scan's SSIDs
// Set while s_scanResults holds a list; Core 1 only, so tick() can drop the
// list once the app leaves without taking the mux every pass.
static bool s_resultsHeld = false;
// The scan in flight was requested by the app, so its list is published.
static bool s_scanForApp = false;
// On lamp::uptimeMs(): either can sit for weeks (home mode off, a client
// connected), and a 32-bit stamp would then read as recent one wrap later.
static uint64_t s_lastScanCompleteMs = 0;
static uint64_t s_lastBackgroundScanMs = 0;
static StateChangeCallback s_cb = nullptr;
static HomeModeEnabledGetter s_homeModeEnabledGetter = nullptr;
static OtaInProgressGetter   s_otaInProgressGetter   = nullptr;
static WebappActiveGetter    s_webappActiveGetter    = nullptr;

// Guards s_scanResults + s_presence + s_lastScanCompleteMs + s_lastError
// against concurrent access. Writers run on Core 1 (wifi::tick scan-complete
// drain + startScan clear, s_lastError set in tick/startSoftAp). Readers on
// Core 0 (BLE WifiStateCallback::onRead → peekScanResults() + lastError()).
// Without this guard, a concurrent push_back / string reassign on Core 1
// while Core 0 copies the vector or string dereferences freed memory.
// homeSsidVisible() also runs on Core 1 (same as tick). Critical sections
// are kept short: O(1) swaps, a fixed-size copy, or a short-string (SSO,
// no-alloc) copy, no network calls, no logging.
static portMUX_TYPE s_scanMux = portMUX_INITIALIZER_UNLOCKED;

// How recent a scan must be for homeSsidVisible() to trust the cache.
// Networks come and go (router restarts, user leaves home), so the
// presence cache times out aggressively.
static constexpr uint32_t SCAN_STALENESS_MS = 90 * 1000;

// How often a background scan fires when idle + no BT client.
// Scans cost ~5s of radio time each. Once a minute is the sweet spot
// between responsiveness and ESP-NOW receive uptime.
static constexpr uint32_t BACKGROUND_SCAN_INTERVAL_MS = 60 * 1000;

// How long to dwell in FAILED before letting the state machine return to
// IDLE so future background scans can be attempted. Without this, a single
// transient `WiFi.scanNetworks()` failure leaves the wifi module stuck in
// FAILED until reboot, and also strands the radio on
// whatever channel the scanner last hopped to, silently killing
// ESP-NOW recv (HELLO / CONTROL_OP / OVERRIDE / EVENT / WISP_HELLO all
// missed). 5 min is long enough not to thrash, short enough that
// home-presence detection comes back without manual intervention.
static constexpr uint32_t FAILED_RETRY_MS = 5 * 60 * 1000;
static uint32_t s_failedSinceMs = 0;

static void setState(State next) {
  if (s_state == next) return;
  s_state = next;
#ifdef LAMP_DEBUG
  Serial.printf("[wifi] state -> %d (%s)\n", (int)next, s_lastError.c_str());
#endif
  if (s_cb) s_cb();
}

void begin() {
  // Order matters: `WiFi.disconnect(true, _)` passes wifioff=true which
  // calls WiFi.mode(WIFI_OFF) on its way out, so STA mode has to be
  // (re-)enabled AFTER any disconnect call. Otherwise WiFi.scanNetworks
  // returns 0 (no STA to scan from).
  // The lamp never reconnects from stored credentials, so the WiFi driver
  // keeps its config in RAM and stops writing the shared NVS partition.
  WiFi.persistent(false);
  WiFi.disconnect(true, true);   // wipe any stale SDK creds from a previous boot
  WiFi.mode(WIFI_STA);            // enable STA so scanNetworks works
  // ESP-NOW is unbuffered, so a wisp/HELLO/OTA frame arriving during a modem
  // sleep window is lost outright. WiFi.mode() resets PS, so re-assert after each.
  WiFi.setSleep(false);
  esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  // Don't run the first periodic scan at boot. The boot-time WiFi stack
  // is fragile (just came up from WiFi.disconnect+WIFI_STA), and a failed
  // scan in this window strands the radio off LAMP_ESPNOW_CHANNEL,
  // killing mesh recv entirely. Seed
  // s_lastBackgroundScanMs with the current uptime so the first
  // periodic scan fires BACKGROUND_SCAN_INTERVAL_MS *after* boot, when
  // the WiFi stack has settled.
  s_lastBackgroundScanMs = lamp::uptimeMs();
}

State state() { return s_state; }

std::string lastError() {
  // s_lastError is always a short literal ("scan" | "softap" | ""), so the
  // copy inside the portMUX stays in SSO and never allocates. Guards a Core 0
  // BLE read from tearing the string while Core 1 reassigns it.
  std::string out;
  portENTER_CRITICAL(&s_scanMux);
  out = s_lastError;
  portEXIT_CRITICAL(&s_scanMux);
  return out;
}

// Frees the published list outside the critical section.
static void dropScanResults() {
  std::vector<ScanResult> drop;
  portENTER_CRITICAL(&s_scanMux);
  drop.swap(s_scanResults);
  portEXIT_CRITICAL(&s_scanMux);
  s_resultsHeld = false;
}

static void beginScan(bool forApp) {
#ifdef LAMP_DEBUG
  Serial.printf("[wifi] scan started forApp=%d\n", (int)forApp);
#endif
  dropScanResults();
  s_scanForApp = forApp;
  WiFi.scanDelete();
  WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
  setState(SCANNING);
}

void startScan() { beginScan(/*forApp=*/true); }

std::vector<ScanResult> peekScanResults() {
  // Non-destructive: both the BLE read and the state-change notify call this,
  // so a consuming drain would let each steal the other's results. Steal the
  // buffer under the mux (O(1)), copy it OUTSIDE the mux (the allocation the
  // portMUX discipline forbids inside a critical section), then restore it
  // unless a fresh scan already published in the gap. The stolen buffer (or a
  // clobbered stale one) frees on return, outside the mux. Results otherwise
  // live until the next scan starts or tick() sees the app gone.
  std::vector<ScanResult> stolen;
  portENTER_CRITICAL(&s_scanMux);
  stolen.swap(s_scanResults);
  portEXIT_CRITICAL(&s_scanMux);
  std::vector<ScanResult> copy = stolen;
  portENTER_CRITICAL(&s_scanMux);
  if (s_scanResults.empty()) s_scanResults.swap(stolen);
  portEXIT_CRITICAL(&s_scanMux);
  return copy;
}

void setStateChangeCallback(StateChangeCallback cb) { s_cb = cb; }

void setHomeModeEnabledGetter(HomeModeEnabledGetter fn) {
  s_homeModeEnabledGetter = fn;
}

void setOtaInProgressGetter(OtaInProgressGetter fn) {
  s_otaInProgressGetter = fn;
}

void setWebappActiveGetter(WebappActiveGetter fn) {
  s_webappActiveGetter = fn;
}

bool homeSsidVisible(const std::string& ssid) {
  if (ssid.empty()) return false;
  // Called from Core 1 (reapplyHomeModeState → currentHomeModeState).
  // The writer is also Core 1 (wifi::tick), so this is single-core, but
  // the wrap stays because s_presence is shared state and the lookup is a
  // bounded, no-alloc scan of SsidPresence::kCapacity hashes.
  bool seen = false;
  uint64_t lastMs = 0;
  portENTER_CRITICAL(&s_scanMux);
  lastMs = s_lastScanCompleteMs;
  if (lastMs != 0) seen = s_presence.contains(ssid);
  portEXIT_CRITICAL(&s_scanMux);
  if (!seen) return false;
  // Staleness check is OUTSIDE the critical section; the clock read is fast
  // but no point holding the lock for a wall-clock read.
  return (lamp::uptimeMs() - lastMs) <= SCAN_STALENESS_MS;
}

static bool s_softApUp = false;
static bool s_softApSecured = false;

bool startSoftAp(const std::string& name, const std::string& psk) {
  if (s_softApUp) return true;
  // AP_STA so STA scan + ESP-NOW recv keep working while the AP serves
  // the webapp. Both interfaces share the radio's current channel;
  // pin LAMP_ESPNOW_CHANNEL after softAPConfig so an associating phone
  // doesn't pull the lamp off the grid channel.
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  // The lamp password keys the AP. Without a usable one the AP is open and
  // every /api route is reachable by any client that associates.
  // Hidden=false so phones surface it without manual entry.
  const bool secured = pskUsable(psk.size());
  const bool ok = WiFi.softAP(name.c_str(), secured ? psk.c_str() : nullptr,
                              LAMP_ESPNOW_CHANNEL,
                              /*ssid_hidden=*/0, /*max_connection=*/4);
  if (!ok) {
    portENTER_CRITICAL(&s_scanMux);
    s_lastError = "softap";
    portEXIT_CRITICAL(&s_scanMux);
#ifdef LAMP_DEBUG
    Serial.println("[wifi] softAP up FAILED");
#endif
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    return false;
  }
  esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  s_softApUp = true;
  s_softApSecured = secured;
#ifdef LAMP_DEBUG
  Serial.printf("[wifi] softAP up ssid=%s ch=%d ip=%s auth=%s\n", name.c_str(),
                LAMP_ESPNOW_CHANNEL, WiFi.softAPIP().toString().c_str(),
                secured ? "wpa2" : "open");
#endif
  return true;
}

void stopSoftAp() {
  if (!s_softApUp) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  // Scanner / mode flip can leave the radio elsewhere; re-pin so ESP-NOW
  // recv resumes immediately (matching the re-pin in the scan-complete
  // branch of tick()).
  esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  s_softApUp = false;
  s_softApSecured = false;
#ifdef LAMP_DEBUG
  Serial.println("[wifi] softAP down");
#endif
}

bool softApSecured() { return s_softApSecured; }

void tick() {
  // 1. Drain a completed scan (whether UI-triggered or background).
  if (s_state == SCANNING) {
    int16_t n = WiFi.scanComplete();
    if (n >= 0) {
      // Build LOCALLY first; no allocations or string ops inside the
      // portMUX. Records arrive strongest first, the order SsidPresence
      // keeps under its cap. Only an app-requested scan builds the list.
      SsidPresence presence;
      std::vector<ScanResult> newResults;
      if (s_scanForApp) newResults.reserve(n);
      for (int16_t i = 0; i < n; i++) {
        auto ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        presence.add(ssid.c_str());
        if (!s_scanForApp) continue;
        newResults.push_back({
          std::string(ssid.c_str()),
          (int8_t)WiFi.RSSI(i),
          WiFi.encryptionType(i) != WIFI_AUTH_OPEN,
        });
      }
      const uint64_t completeMs = lamp::uptimeMs();
      // Publish: swap in the new list and free the old one OUTSIDE the
      // critical section.
      std::vector<ScanResult> dropResults;
      portENTER_CRITICAL(&s_scanMux);
      dropResults.swap(s_scanResults);
      s_scanResults.swap(newResults);
      s_presence = presence;
      s_lastScanCompleteMs = completeMs;
      portEXIT_CRITICAL(&s_scanMux);
      s_resultsHeld = s_scanForApp;
#ifdef LAMP_DEBUG
      Serial.printf("[wifi] scan done: records=%d distinct=%u overflow=%d\n", (int)n,
                    (unsigned)presence.size(), (int)presence.overflowed());
#endif
      WiFi.scanDelete();
      // Re-pin to grid channel post-scan (scanNetworks may have left the
      // radio on whichever channel it finished on).
      esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
      setState(IDLE);
    } else if (n == WIFI_SCAN_FAILED) {
      portENTER_CRITICAL(&s_scanMux);
      s_lastError = "scan";
      portEXIT_CRITICAL(&s_scanMux);
      // The scanner can leave the radio on the last-hopped channel; re-pin
      // to LAMP_ESPNOW_CHANNEL so ESP-NOW recv still works in FAILED.
      // Without this re-pin, a failed scan silently breaks all mesh recv
      // until reboot.
      esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
      s_failedSinceMs = millis();
      setState(FAILED);
    }
  }

  // FAILED → IDLE recovery so a transient scan failure doesn't permanently
  // disable home-presence detection. The channel re-pin in the FAILED
  // branch above keeps mesh recv alive in the meantime.
  if (s_state == FAILED && millis() - s_failedSinceMs > FAILED_RETRY_MS) {
    portENTER_CRITICAL(&s_scanMux);
    s_lastError.clear();
    portEXIT_CRITICAL(&s_scanMux);
    setState(IDLE);
  }

  // 2. Periodic background scan for home-presence detection. Gated on:
  //    - no BT client connected (scanning during a BT session stresses
  //      the shared radio)
  //    - home-mode is enabled in config (otherwise scan results have no
  //      consumer; no point spending radio time on them)
  //    - BACKGROUND_SCAN_INTERVAL_MS elapsed since the last scan
  //
  //    The first periodic scan fires BACKGROUND_SCAN_INTERVAL_MS after
  //    boot. See wifi::begin() for the rationale (boot-
  //    time scan failures stranded the radio off LAMP_ESPNOW_CHANNEL).
  const bool homeModeEnabled =
      s_homeModeEnabledGetter && s_homeModeEnabledGetter();
  // Block periodic scans during OTA. Channel hopping silently drops
  // ESP-NOW unicast in both directions for the duration of the scan.
  // On-demand scans (BLE op:scan) still
  // route through startScan() directly and remain available.
  const bool otaInProgress =
      s_otaInProgressGetter && s_otaInProgressGetter();
  // Block periodic scans while a webapp client is connected: the channel hop
  // stalls the softAP's HTTP server for the scan's duration.
  const bool webappActive =
      s_webappActiveGetter && s_webappActiveGetter();
  const bool clientConnected = ble_control::isClientConnected();
  // The list serves only the app; drop it once the app has gone.
  if (s_resultsHeld && !clientConnected) dropScanResults();
  if (s_state == IDLE && !clientConnected &&
      homeModeEnabled && !otaInProgress && !webappActive) {
    const uint64_t now = lamp::uptimeMs();
    const uint64_t elapsed = now - s_lastBackgroundScanMs;
    if (elapsed > BACKGROUND_SCAN_INTERVAL_MS) {
#ifdef LAMP_DEBUG
      Serial.printf("[wifi.sched] scan DECIDED elapsed=%u lastBgMs=%u now=%u\n",
                    (unsigned)elapsed,
                    (unsigned)s_lastBackgroundScanMs, (unsigned)now);
#endif
      s_lastBackgroundScanMs = now;
      beginScan(/*forApp=*/false);
    }
  }
}

}  // namespace wifi
