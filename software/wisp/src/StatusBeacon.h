// StatusBeacon — broadcasts wisp presence + state to the lamp grid.
//
// Two emission paths, distinct cadences, distinct frame formats:
//
//   1. MSG_WISP_HELLO (binary, 37 bytes) — every kHelloIntervalMs (2s).
//      Compact presence beacon. Receivers use it to populate the wisp
//      inventory and to gate paint-mode behavior.
//
//   2. MSG_CONTROL_OP (JSON, `char:"wispStatus"`) — on-change + 30s
//      heartbeat. Carries currentZone, zoneSource, observedZones[],
//      wifiConnected, auroraConnected, paletteIdPrefix, lastSeenMs so the
//      Flutter app pane (proxied via a nearby lamp's CHAR_WISP_STATUS BLE
//      characteristic) can render and edit the wisp's selection.
//
//      Cadence rationale: every wispStatus broadcast is gossip-relayed by
//      lamps, so in a 22-lamp fleet each emit becomes up to ~22 in-air
//      frames and consumes a slot in every lamp's 64-slot controlOpDedup_.
//      5s would compete with operator CONTROL_OPs for dedup capacity and
//      add meaningful airtime. On-change pushes keep the UI responsive;
//      the 30s heartbeat is staleness insurance.
//
// Both paths fire from FreeRTOS software timers, NOT from loop(), because
// the Aurora client's loop() can stall for tens-to-hundreds of ms while
// reading from the WebSocket (under bad WiFi conditions, easily 1s+). The
// timer service runs on a dedicated task, immune to loop() stalls.
//
// THREADING: emit() and emitStatus() may run from either the timer-service
// task (heartbeat path) or the loop task (triggerOnChange path). Both
// touch seqCounter_ and read from sibling components. A portMUX guards the
// two emission bodies — short critical sections, no logging inside.

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <freertos/portmacro.h>
using StatusBeaconTimerHandle = TimerHandle_t;
#define STATUS_BEACON_PORTMUX_TYPE       portMUX_TYPE
#define STATUS_BEACON_PORTMUX_INIT       portMUX_INITIALIZER_UNLOCKED
#define STATUS_BEACON_PORTMUX_ENTER(mux) portENTER_CRITICAL(mux)
#define STATUS_BEACON_PORTMUX_EXIT(mux)  portEXIT_CRITICAL(mux)
#else
// Native test build doesn't include this file — but if it ever did, give
// the symbol a placeholder so nothing trips. Tests should not instantiate
// StatusBeacon; the class touches FreeRTOS + WiFi.
using StatusBeaconTimerHandle = void*;
struct StatusBeaconNullMux {};
#define STATUS_BEACON_PORTMUX_TYPE       StatusBeaconNullMux
#define STATUS_BEACON_PORTMUX_INIT       {}
#define STATUS_BEACON_PORTMUX_ENTER(mux) ((void)(mux))
#define STATUS_BEACON_PORTMUX_EXIT(mux)  ((void)(mux))
#endif

class AuroraPaletteClient;

namespace wisp {

class CurrentPalette;
class MeshLink;
class PaintDistributor;
class WispConfig;
class WispRoster;
class ZoneSelector;

class StatusBeacon {
 public:
  // `zone` and `aurora` are used by the wispStatus path; the HELLO path
  // doesn't need them.
  //
  // `config` is optional for legacy callers; when non-null, the
  // wispStatus emit includes the `source` enum so the app can surface
  // and round-trip the source-toggle state. (Manual palette body is
  // intentionally not echoed; see emitStatus comment for the
  // CONTROL_MAX_PAYLOAD budget.)
  //
  // MSG_WISP_HELLO's carriedFw* fields zero-fill — the wisp no longer
  // distributes firmware (gossip-OTA on lamps replaces it). Wire layout
  // unchanged for back-compat with older lamp firmware.
  void begin(MeshLink* mesh, PaintDistributor* paint,
             CurrentPalette* palette, ZoneSelector* zone,
             AuroraPaletteClient* aurora,
             WispConfig* config = nullptr,
             WispRoster* roster = nullptr);

  // One-shot wiring of the two FreeRTOS software timers. Call once after
  // begin().
  void startTimer();

  // Force an immediate wispStatus emit AND reschedule the heartbeat from
  // now. Call after applying a setZone/clearZone/setWifi op so the app sees
  // the new state without waiting up to 30s. Safe from the loop task.
  void triggerOnChange();

  // Public so the C-style timer trampolines can reach them. Not part of the
  // sanctioned API — call begin()+startTimer() from main.cpp and forget.
  void emit();         // MSG_WISP_HELLO + MSG_WISP_CLAIM (2s cadence)
  void emitStatus();   // MSG_CONTROL_OP wispStatus path (30s heartbeat)
  void emitPalette();  // MSG_WISP_PALETTE (piggybacked on emitStatus)

 private:
  MeshLink* mesh_ = nullptr;
  PaintDistributor* paint_ = nullptr;
  CurrentPalette* palette_ = nullptr;
  ZoneSelector* zone_ = nullptr;
  AuroraPaletteClient* aurora_ = nullptr;
  WispConfig* config_ = nullptr;
  WispRoster* roster_ = nullptr;
  uint16_t seqCounter_ = 0;          // shared across both emission paths
  StatusBeaconTimerHandle timer_       = nullptr;  // 2s HELLO
  StatusBeaconTimerHandle statusTimer_ = nullptr;  // 30s wispStatus heartbeat

  // Diff-state for the wifi/aurora on-change detection inside emitStatus.
  // First emit forces a broadcast regardless. Subsequent emits use these
  // to gate logging; the broadcast itself is unconditional on the timer
  // tick (it's at 30s cadence, work is minimal) and additionally
  // triggered by main.cpp's drainPendingWispOp for sub-30s zone-pick
  // responsiveness.
  bool lastWifiConnected_   = false;
  bool lastAuroraConnected_ = false;
  bool haveLastConnState_   = false;

  // Diff-state for the 2s HELLO path. Separate from the emitStatus log-
  // gating vars above so the two on-change paths don't interfere: the
  // hello-timer slot diffs WiFi + Aurora and, on flip, drives an immediate
  // emitStatus() so passive radio events propagate within ~2s instead of
  // waiting up to 30s for the heartbeat.
  bool lastHelloWifi_      = false;
  bool lastHelloAurora_    = false;
  bool haveLastHelloConn_  = false;

  // Guards seqCounter_ and the two emission bodies. Critical sections are
  // short (build frame on a stack buffer, hand to MeshLink::broadcast which
  // just enqueues to esp_now_send). Don't log inside the lock.
  STATUS_BEACON_PORTMUX_TYPE emitMux_ = STATUS_BEACON_PORTMUX_INIT;

  static constexpr uint32_t kHelloIntervalMs  = 2000;
  static constexpr uint32_t kStatusIntervalMs = 30000;
  static constexpr uint32_t kWispVersion      = 0x00010000u;  // 1.0.0

  // wispStatus JSON serialization budget. CONTROL_MAX_PAYLOAD is 230;
  // worst-case observedZones[16] with two-digit ids fits well under that.
  // Concrete worst case calculation lives in the .cpp comment.
  static constexpr size_t kStatusJsonBufLen = 256;
};

}  // namespace wisp
