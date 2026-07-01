// PresenceBeacon — wisp presence on a 2s FreeRTOS timer.
//
// Broadcasts MSG_WISP_HELLO then MSG_WISP_CLAIM (roster snapshot). A WiFi/Aurora
// flag flip triggers StatusEmitter's on-change path so the app sees it within 2s
// rather than waiting on the 30s heartbeat. Runs from its timer task; the
// SeqSource mux guards seq bump + frame build.

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
using PresenceBeaconTimerHandle = TimerHandle_t;
#else
using PresenceBeaconTimerHandle = void*;
#endif

class AuroraPaletteClient;

namespace wisp {

class CurrentPalette;
class MeshLink;
class PaintDistributor;
class StatusEmitter;
class WispRoster;
struct SeqSource;

class PresenceBeacon {
 public:
  void begin(MeshLink* mesh, PaintDistributor* paint, CurrentPalette* palette,
             AuroraPaletteClient* aurora, WispRoster* roster, SeqSource* seq,
             StatusEmitter* statusEmitter);

  // One-shot wiring of the 2s HELLO timer. Call once after begin().
  void startTimer();

  // Public so the C-style timer trampoline can reach it; not a sanctioned API.
  void emit();  // MSG_WISP_HELLO + MSG_WISP_CLAIM (2s cadence)

 private:
  MeshLink* mesh_ = nullptr;
  PaintDistributor* paint_ = nullptr;
  CurrentPalette* palette_ = nullptr;
  AuroraPaletteClient* aurora_ = nullptr;
  WispRoster* roster_ = nullptr;
  SeqSource* seq_ = nullptr;
  StatusEmitter* statusEmitter_ = nullptr;
  PresenceBeaconTimerHandle timer_ = nullptr;

  // A WiFi/Aurora flip in HELLO triggers StatusEmitter within 2s rather than
  // waiting 30s.
  bool lastHelloWifi_     = false;
  bool lastHelloAurora_   = false;
  bool haveLastHelloConn_ = false;

  static constexpr uint32_t kHelloIntervalMs = 2000;
  static constexpr uint32_t kWispVersion     = 0x00010000u;  // 1.0.0
};

}  // namespace wisp
