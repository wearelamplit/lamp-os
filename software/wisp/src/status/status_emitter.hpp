// StatusEmitter drives the wispStatus heartbeat on a 30s FreeRTOS timer.
//
// Broadcasts MSG_CONTROL_OP (wispStatus JSON) then MSG_WISP_PALETTE. The timer,
// requestEmit and triggerOnChange only flag the emit due; pump() runs the
// build + broadcast on the loop task so nothing heavy touches the 2KB timer
// stack. pump() also detects wifi connectivity edges and requests an emit for
// them. StatusCadence decides when a status goes out; wispConfig follows on
// the cadence ConfigCadence sets, always in a separate pump pass from the
// status.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "status/config_cadence.hpp"
#include "status/status_cadence.hpp"
#include "status/status_json.hpp"
#include "wire/lamp_protocol.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
using StatusEmitterTimerHandle = TimerHandle_t;
#else
using StatusEmitterTimerHandle = void*;
#endif

namespace wisp {

class CurrentPalette;
class MeshLink;
class WifiLink;
class WispConfig;
class WispController;
struct SeqSource;

class StatusEmitter {
 public:
  void begin(MeshLink* mesh, const WispController* controller,
             WispConfig* config, CurrentPalette* palette, SeqSource* seq,
             WifiLink* wifi);

  // One-shot wiring of the 30s heartbeat timer. Call once after begin().
  void startTimer();

  // Mark one wispStatus emit due, with no burst. For a read-only poll or a
  // connectivity / source-liveness edge, so the app sees it without waiting
  // up to 30s. Safe from any task; the emit runs from pump().
  void requestEmit();

  // Mark a wispStatus emit due, open the 20 s re-send burst and reschedule
  // the heartbeat from now. Call after applying an op that changes reported
  // state, so the app's confirmation survives a coex reception gap. The emit
  // runs from pump() on the loop task.
  void triggerOnChange();

  // Mark a wispConfig emit due (pollConfig). Safe from any task; the emit
  // runs from pump(), coalesced by ConfigCadence.
  void pollConfig();

  // Runs the due wispStatus / wispConfig emit on the loop task. Cheap when
  // nothing is due. True when it ran a wispStatus or wispConfig emit this pass.
  bool pump();

 private:
  // wifi connectivity flips flag an emit. Loop task only.
  void checkConnEdges();

  // Emit bodies run only from pump() on the loop task.
  void emitStatus();   // MSG_CONTROL_OP wispStatus path (30s heartbeat)
  void emitPalette();  // MSG_WISP_PALETTE (piggybacked on emitStatus)
  void emitConfig();   // MSG_CONTROL_OP wispConfig path

  WispConfigFields configFields() const;
  size_t buildConfig(char* out, size_t outCap) const;
  void broadcastControlOp(const char* json, size_t jsonLen, const char* what);

  MeshLink* mesh_ = nullptr;
  const WispController* controller_ = nullptr;
  WispConfig* config_ = nullptr;
  CurrentPalette* palette_ = nullptr;
  SeqSource* seq_ = nullptr;
  WifiLink* wifi_ = nullptr;
  StatusEmitterTimerHandle statusTimer_ = nullptr;

  std::atomic<bool> configPollDue_{false};

  bool lastWifiConnected_ = false;
  bool lastEthUp_ = false;
  bool haveLastConnState_ = false;

  StatusCadence statusCadence_;
  ConfigCadence configCadence_;

  static constexpr uint32_t kStatusIntervalMs = 30000;
  static constexpr size_t kStatusJsonBufLen =
      lamp_protocol::CONTROL_MAX_PAYLOAD + 1;
  static constexpr size_t kConfigJsonBufLen = 256;
};

}  // namespace wisp
