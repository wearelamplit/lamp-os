// StatusEmitter — wispStatus heartbeat on a 30s FreeRTOS timer.
//
// Broadcasts MSG_CONTROL_OP (wispStatus JSON) then MSG_WISP_PALETTE. Fires from
// its timer task and from the loop task (triggerOnChange); the SeqSource mux
// guards seq bump + frame build.

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
using StatusEmitterTimerHandle = TimerHandle_t;
#else
using StatusEmitterTimerHandle = void*;
#endif

class AuroraPaletteClient;

namespace wisp {

class CurrentPalette;
class MeshLink;
class WispConfig;
class ZoneSelector;
struct SeqSource;

class StatusEmitter {
 public:
  void begin(MeshLink* mesh, ZoneSelector* zone, AuroraPaletteClient* aurora,
             WispConfig* config, CurrentPalette* palette, SeqSource* seq);

  // One-shot wiring of the 30s heartbeat timer. Call once after begin().
  void startTimer();

  // Force an immediate wispStatus emit AND reschedule the heartbeat from now.
  // Call after applying a setZone/clearZone/setWifi op so the app sees the new
  // state without waiting up to 30s. Safe from the loop task.
  void triggerOnChange();

  // Public so the C-style timer trampoline can reach them; not a sanctioned API.
  void emitStatus();   // MSG_CONTROL_OP wispStatus path (30s heartbeat)
  void emitPalette();  // MSG_WISP_PALETTE (piggybacked on emitStatus)

 private:
  MeshLink* mesh_ = nullptr;
  ZoneSelector* zone_ = nullptr;
  AuroraPaletteClient* aurora_ = nullptr;
  WispConfig* config_ = nullptr;
  CurrentPalette* palette_ = nullptr;
  SeqSource* seq_ = nullptr;
  StatusEmitterTimerHandle statusTimer_ = nullptr;

  bool lastWifiConnected_   = false;
  bool lastAuroraConnected_ = false;
  bool haveLastConnState_   = false;

  static constexpr uint32_t kStatusIntervalMs = 30000;
  static constexpr size_t kStatusJsonBufLen = 256;
};

}  // namespace wisp
