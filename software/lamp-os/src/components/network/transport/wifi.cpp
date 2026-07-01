#include "wifi.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <portmacro.h>

#include "components/network/ble/ble_control.hpp"  // for ble_control::isClientConnected()

// Grid channel — all unconnected grid lamps line up here so ESP-NOW peers
// can hear each other. Since we never associate to a home AP anymore,
// the radio sits on this channel permanently (modulo brief scan windows).
#ifndef LAMP_ESPNOW_CHANNEL
// Moved 1 → 11 on 2026-06-10. Mirror of mesh_link.hpp / MeshLink.h.
#define LAMP_ESPNOW_CHANNEL 11
#endif

namespace wifi {

static State s_state = IDLE;
static std::string s_lastError;
static std::vector<ScanResult> s_scanResults;          // drained by UI notify
static std::vector<std::string> s_recentSsids;         // persistent presence cache
static uint32_t s_lastScanCompleteMs = 0;
static uint32_t s_lastBackgroundScanMs = 0;
static StateChangeCallback s_cb = nullptr;
static HomeModeEnabledGetter s_homeModeEnabledGetter = nullptr;
static OtaInProgressGetter   s_otaInProgressGetter   = nullptr;

// Guards s_scanResults + s_recentSsids + s_lastScanCompleteMs against
// concurrent access. Writer: Core 1 wifi::tick() (scan-complete drain
// + startScan clear). Reader/drainer: Core 0 BLE WifiStateCallback::onRead
// → consumeScanResults() (std::move + clear). Without this guard, a
// concurrent push_back on Core 1 while Core 0 std::move's the vector
// dereferences freed memory. Same homeSsidVisible() runs on Core 1
// (same as tick) but we wrap defensively — critical sections are short
// (no allocations beyond the std::move which steals the pointer, no
// network calls, no logging). See audit finding #7 / Stability #4.
static portMUX_TYPE s_scanMux = portMUX_INITIALIZER_UNLOCKED;

// How recent a scan must be for homeSsidVisible() to trust the cache.
// Networks come and go (router restarts, user leaves home), so we time
// out the presence cache aggressively.
static constexpr uint32_t SCAN_STALENESS_MS = 90 * 1000;

// How often we kick off a background scan when idle + no BT client.
// Scans cost ~5s of radio time each — once a minute is the sweet spot
// between responsiveness and ESP-NOW receive uptime.
static constexpr uint32_t BACKGROUND_SCAN_INTERVAL_MS = 60 * 1000;

// How long to dwell in FAILED before letting the state machine return to
// IDLE so future background scans can be attempted. Without this, a single
// transient `WiFi.scanNetworks()` failure leaves the wifi module stuck in
// FAILED until reboot — and historically also stranded the radio on
// whatever channel the scanner had last hopped to, silently killing
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
  // (re-)enabled AFTER any disconnect call — otherwise WiFi.scanNetworks
  // returns 0 (no STA to scan from).
  WiFi.disconnect(true, true);   // wipe any stale SDK creds from a previous boot
  WiFi.mode(WIFI_STA);            // enable STA so scanNetworks works
  esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  // Don't run the first periodic scan at boot. The boot-time WiFi stack
  // is fragile (just came up from WiFi.disconnect+WIFI_STA), and a failed
  // scan in this window strands the radio off LAMP_ESPNOW_CHANNEL —
  // observed 2026-06-04 on jacko, killed mesh recv entirely. Seed
  // s_lastBackgroundScanMs with the current millis() so the first
  // periodic scan fires BACKGROUND_SCAN_INTERVAL_MS *after* boot, when
  // the WiFi stack has settled.
  s_lastBackgroundScanMs = millis();
}

State state() { return s_state; }
std::string lastError() { return s_lastError; }

void startScan() {
#ifdef LAMP_DEBUG
  // Logged so we can rule scans in/out of any "slow during BT" reports
  // — scans only fire when BT is disconnected, but a scan started just
  // before a reconnect can spill ~5s into the BT session.
  Serial.println("[wifi] scan started");
  // Trace which gate fired this scan (added 2026-06-04 for scan-storm
  // diagnosis). lastBgMs is updated by the periodic gate immediately
  // before this call, so if it's "now-ish" the caller is the periodic
  // path; if much older, the caller was external (e.g. BLE op:scan).
  Serial.printf("[wifi.sched] startScan() entry: lastBgMs=%u now=%u\n",
                (unsigned)s_lastBackgroundScanMs, (unsigned)millis());
#endif
  // Steal the existing results buffer, free OUTSIDE the critical section.
  std::vector<ScanResult> drop;
  portENTER_CRITICAL(&s_scanMux);
  drop.swap(s_scanResults);
  portEXIT_CRITICAL(&s_scanMux);
  WiFi.scanDelete();
  WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
  setState(SCANNING);
}

std::vector<ScanResult> consumeScanResults() {
  // Called from Core 0 (BLE WifiStateCallback::onRead). The swap is
  // O(1) — three-pointer steal — so the critical section is tight.
  // Destructors on the moved-out empty vector are trivial.
  std::vector<ScanResult> out;
  portENTER_CRITICAL(&s_scanMux);
  out.swap(s_scanResults);
  portEXIT_CRITICAL(&s_scanMux);
  return out;
}

void setStateChangeCallback(StateChangeCallback cb) { s_cb = cb; }

void setHomeModeEnabledGetter(HomeModeEnabledGetter fn) {
  s_homeModeEnabledGetter = fn;
}

void setOtaInProgressGetter(OtaInProgressGetter fn) {
  s_otaInProgressGetter = fn;
}

bool homeSsidVisible(const std::string& ssid) {
  if (ssid.empty()) return false;
  // Called from Core 1 (reapplyHomeModeState → calculateEffectiveHomeMode).
  // The writer is also Core 1 (wifi::tick), so this is single-core — but
  // we still wrap because s_recentSsids is shared state and the cost is
  // negligible (string == is a no-alloc compare). The string compares run
  // inside the critical section; the loop is bounded (~30 SSIDs max).
  bool seen = false;
  uint32_t lastMs = 0;
  portENTER_CRITICAL(&s_scanMux);
  lastMs = s_lastScanCompleteMs;
  if (lastMs != 0) {
    for (const auto& s : s_recentSsids) {
      if (s == ssid) { seen = true; break; }
    }
  }
  portEXIT_CRITICAL(&s_scanMux);
  if (!seen) return false;
  // Staleness check is OUTSIDE the critical section — millis() is fast
  // but no point holding the lock for a wall-clock read.
  return (millis() - lastMs) <= SCAN_STALENESS_MS;
}

static bool s_softApUp = false;

bool startSoftAp(const std::string& name) {
  if (s_softApUp) return true;
  // AP_STA so STA scan + ESP-NOW recv keep working while the AP serves
  // the webapp. Both interfaces share the radio's current channel — we
  // pin LAMP_ESPNOW_CHANNEL after softAPConfig so an associating phone
  // doesn't pull the lamp off the grid channel.
  WiFi.mode(WIFI_AP_STA);
  // Open network (no password) — the boot window IS the auth boundary.
  // Hidden=false so phones surface it without manual entry.
  const bool ok = WiFi.softAP(name.c_str(), nullptr, LAMP_ESPNOW_CHANNEL,
                              /*ssid_hidden=*/0, /*max_connection=*/4);
  if (!ok) {
    s_lastError = "softap";
#ifdef LAMP_DEBUG
    Serial.println("[wifi] softAP up FAILED");
#endif
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    return false;
  }
  esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  s_softApUp = true;
#ifdef LAMP_DEBUG
  Serial.printf("[wifi] softAP up ssid=%s ch=%d ip=%s\n", name.c_str(),
                LAMP_ESPNOW_CHANNEL, WiFi.softAPIP().toString().c_str());
#endif
  return true;
}

void stopSoftAp() {
  if (!s_softApUp) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  // Scanner / mode flip can leave the radio elsewhere; re-pin so ESP-NOW
  // recv resumes immediately (same defensive re-pin pattern as the
  // scan-complete branch of tick()).
  esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  s_softApUp = false;
#ifdef LAMP_DEBUG
  Serial.println("[wifi] softAP down");
#endif
}

void tick() {
  // 1. Drain a completed scan (whether UI-triggered or background).
  if (s_state == SCANNING) {
    int16_t n = WiFi.scanComplete();
    if (n >= 0) {
      // Build into LOCAL vectors first — no allocations or string ops
      // inside the portMUX. The atomic swap at the end is O(1).
      std::vector<ScanResult> newResults;
      std::vector<std::string> newSsids;
      newResults.reserve(n);
      newSsids.reserve(n);
      for (int16_t i = 0; i < n; i++) {
        auto ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        std::string ssidStr(ssid.c_str());
        newResults.push_back({
          ssidStr,
          (int8_t)WiFi.RSSI(i),
          WiFi.encryptionType(i) != WIFI_AUTH_OPEN,
        });
        newSsids.push_back(ssidStr);
      }
      const uint32_t completeMs = millis();
      // Atomic publish: swap in the new buffers, free the old ones
      // OUTSIDE the critical section.
      std::vector<ScanResult> dropResults;
      std::vector<std::string> dropSsids;
      portENTER_CRITICAL(&s_scanMux);
      dropResults.swap(s_scanResults);
      dropSsids.swap(s_recentSsids);
      s_scanResults.swap(newResults);
      s_recentSsids.swap(newSsids);
      s_lastScanCompleteMs = completeMs;
      portEXIT_CRITICAL(&s_scanMux);
      WiFi.scanDelete();
      // Re-pin to grid channel post-scan (scanNetworks may have left the
      // radio on whichever channel it finished on).
      esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
      setState(IDLE);
    } else if (n == WIFI_SCAN_FAILED) {
      s_lastError = "scan";
      // The scanner can leave the radio on the last-hopped channel; re-pin
      // to LAMP_ESPNOW_CHANNEL so ESP-NOW recv still works in FAILED.
      // Symmetry with the success branch above. Before this re-pin, a
      // failed scan silently broke ALL mesh recv on this lamp until
      // reboot (observed on melonie 2026-06-03 — 2 of 21 MSG_EVENT
      // broadcasts received during a cascade test).
      esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
      s_failedSinceMs = millis();
      setState(FAILED);
    }
  }

  // FAILED → IDLE recovery so a transient scan failure doesn't permanently
  // disable home-presence detection. The channel re-pin in the FAILED
  // branch above keeps mesh recv alive in the meantime.
  if (s_state == FAILED && millis() - s_failedSinceMs > FAILED_RETRY_MS) {
    s_lastError.clear();
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
  //    boot, not at boot — see wifi::begin() for the rationale (boot-
  //    time scan failures stranded the radio off LAMP_ESPNOW_CHANNEL).
  const bool homeModeEnabled =
      s_homeModeEnabledGetter && s_homeModeEnabledGetter();
  // Block periodic scans during OTA — channel hopping silently drops
  // ESP-NOW unicast in both directions for the duration of the scan
  // (hardware-confirmed 2026-06-04). On-demand scans (BLE op:scan) still
  // route through startScan() directly and remain available.
  const bool otaInProgress =
      s_otaInProgressGetter && s_otaInProgressGetter();
  if (s_state == IDLE && !ble_control::isClientConnected() &&
      homeModeEnabled && !otaInProgress) {
    const uint32_t now = millis();
    const uint32_t elapsed = now - s_lastBackgroundScanMs;
    if (elapsed > BACKGROUND_SCAN_INTERVAL_MS) {
#ifdef LAMP_DEBUG
      Serial.printf("[wifi.sched] scan DECIDED elapsed=%u lastBgMs=%u now=%u\n",
                    (unsigned)elapsed,
                    (unsigned)s_lastBackgroundScanMs, (unsigned)now);
#endif
      s_lastBackgroundScanMs = now;
      startScan();
    }
  }
}

}  // namespace wifi
