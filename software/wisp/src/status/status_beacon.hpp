// StatusBeacon — broadcasts wisp presence + state to the lamp grid.
//
// MSG_WISP_HELLO every 2s; MSG_CONTROL_OP (wispStatus JSON) on-change + 30s
// heartbeat. Both fire from FreeRTOS timers because Aurora's loop() can stall
// 1s+ on bad WiFi. emit()/emitStatus() run from timer task or loop task
// (triggerOnChange); portMUX guards seqCounter_ and emission bodies.

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
// Native build doesn't include this file; the no-op mux just keeps the
// symbol defined. StatusBeacon touches FreeRTOS + WiFi, don't instantiate it
// in tests.
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

  // Public so the C-style timer trampolines can reach them; not a sanctioned
  // API (call begin()+startTimer() from main.cpp).
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

  // Diff-state for wifi/aurora on-change logging inside emitStatus. The
  // broadcast itself is unconditional on the timer tick; these only gate the
  // log lines.
  bool lastWifiConnected_   = false;
  bool lastAuroraConnected_ = false;
  bool haveLastConnState_   = false;

  // HELLO-path diff-state kept separate from emitStatus vars: a WiFi/Aurora
  // flip in HELLO triggers emitStatus() within 2s rather than waiting 30s.
  bool lastHelloWifi_      = false;
  bool lastHelloAurora_    = false;
  bool haveLastHelloConn_  = false;

  // No logging inside the lock; critical section builds frame on stack only.
  STATUS_BEACON_PORTMUX_TYPE emitMux_ = STATUS_BEACON_PORTMUX_INIT;

  static constexpr uint32_t kHelloIntervalMs  = 2000;
  static constexpr uint32_t kStatusIntervalMs = 30000;
  static constexpr uint32_t kWispVersion      = 0x00010000u;  // 1.0.0

  static constexpr size_t kStatusJsonBufLen = 256;
};

}  // namespace wisp
