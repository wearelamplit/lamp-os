// StatusEmitter — wispStatus heartbeat on a 30s FreeRTOS timer.
//
// Broadcasts MSG_CONTROL_OP (wispStatus JSON) then MSG_WISP_PALETTE. The timer
// (and triggerOnChange) only flag the emit due; pump() runs the build +
// broadcast on the loop task so nothing heavy touches the 2KB timer stack.

#pragma once

#include <atomic>
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

  // Mark a wispStatus emit due AND reschedule the heartbeat from now. Call
  // after applying a setZone/clearZone/setWifi op so the app sees the new state
  // without waiting up to 30s. The emit runs from pump() on the loop task, so
  // this is safe from any task and does no heavy work itself.
  void triggerOnChange();

  // Runs the due wispStatus emit on the loop task. Cheap when nothing is due.
  void pump();

 private:
  // Emit bodies run only from pump() on the loop task.
  void emitStatus();   // MSG_CONTROL_OP wispStatus path (30s heartbeat)
  void emitPalette();  // MSG_WISP_PALETTE (piggybacked on emitStatus)

  MeshLink* mesh_ = nullptr;
  ZoneSelector* zone_ = nullptr;
  AuroraPaletteClient* aurora_ = nullptr;
  WispConfig* config_ = nullptr;
  CurrentPalette* palette_ = nullptr;
  SeqSource* seq_ = nullptr;
  StatusEmitterTimerHandle statusTimer_ = nullptr;

  // Set from the timer task / triggerOnChange, cleared in pump() on the loop
  // task, so the ~750B JSON build + broadcast never runs on the 2KB timer
  // daemon stack.
  std::atomic<bool> statusDue_{false};

  bool lastWifiConnected_   = false;
  bool lastAuroraConnected_ = false;
  bool haveLastConnState_   = false;

  static constexpr uint32_t kStatusIntervalMs = 30000;
  static constexpr size_t kStatusJsonBufLen = 256;
};

}  // namespace wisp
