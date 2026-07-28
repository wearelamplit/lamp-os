// PresenceBeacon broadcasts wisp presence on a 2s FreeRTOS timer.
//
// MSG_WISP_HELLO fires every 2s tick; the fat full-set MSG_WISP_CLAIM and
// MSG_WISP_STATE frames alternate on odd/even ticks so two fat frames never
// fire in the same tick on the single core. STATE is the sole paint render
// path (per-lamp colors), so a paint-mode edge also emits STATE out of cadence
// via PaintDistributor's dirty flag. A WiFi/Aurora flag flip triggers
// StatusEmitter's on-change path so the app sees it within 2s rather than
// waiting on the 30s heartbeat. The timer only flags the emit due; pump() runs
// the build + broadcast on the loop task, off the 2KB timer stack.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "version.hpp"
#include "wire/lamp_protocol.hpp"

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
class WispConfig;
class WispRoster;
struct SeqSource;

class PresenceBeacon {
 public:
  void begin(MeshLink* mesh, PaintDistributor* paint, CurrentPalette* palette,
             AuroraPaletteClient* aurora, WispRoster* roster, SeqSource* seq,
             StatusEmitter* statusEmitter, WispConfig* config);

  // Call once after begin().
  void startTimer();

  // Runs the due HELLO emit on the loop task. Cheap when nothing is due.
  void pump();

 private:
  void emit();                              // HELLO every 2s + alternating claim/state
  void emitClaim(const uint8_t srcMac[6]);  // full-set MSG_WISP_CLAIM
  // full-set MSG_WISP_STATE; presenceFlags carries the same WISP_HELLO_FLAG_*
  // bits computed for the HELLO this tick. Packs color entries only while
  // paint is on, so Off broadcasts an empty set and lamps ease home.
  void emitState(const uint8_t srcMac[6], uint8_t presenceFlags);
  // Out-of-cadence STATE for a paint-mode edge; recomputes mac + flags.
  void emitStateEvent();
  uint8_t computePresenceFlags(bool wifiNow, bool auroraNow) const;

  // Re-broadcast a just-sent frame (same seq) `copies` more times, kResendGapMs
  // apart: an unacked broadcast has no retry, so a receiver whose scan-listen
  // gap swallows one copy still catches another. Dedup collapses them to one.
  void queueResend(const uint8_t* frame, size_t len, uint8_t copies, uint32_t now);
  void serviceResends(uint32_t now);

  MeshLink* mesh_ = nullptr;
  PaintDistributor* paint_ = nullptr;
  CurrentPalette* palette_ = nullptr;
  AuroraPaletteClient* aurora_ = nullptr;
  WispRoster* roster_ = nullptr;
  SeqSource* seq_ = nullptr;
  StatusEmitter* statusEmitter_ = nullptr;
  WispConfig* config_ = nullptr;
  PresenceBeaconTimerHandle timer_ = nullptr;

  // Set from the timer task, cleared in pump() on the loop task.
  std::atomic<bool> helloDue_{false};

  bool lastHelloWifi_     = false;
  bool lastHelloAurora_   = false;
  bool haveLastHelloConn_ = false;

  // 2s-tick counter driving the phased claim/paint sub-multiples.
  uint32_t beaconTick_ = 0;

  struct ResendSlot {
    // CLAIM is the largest resent frame; STATE is not resent (too big, and it
    // re-covers on its own cadence).
    uint8_t  frame[lamp_protocol::WISP_CLAIM_MAX_SIZE];
    uint16_t len = 0;
    uint8_t  remaining = 0;
    uint32_t nextMs = 0;
  };
  // HELLO + at most one alternating fat frame emit per tick, so 3 slots never
  // collide before the resends drain well within the 2s cadence.
  static constexpr size_t   kResendSlots  = 3;
  static constexpr uint8_t  kHelloResends = 2;
  static constexpr uint8_t  kFatResends   = 1;
  static constexpr uint32_t kResendGapMs  = 40;
  ResendSlot resends_[kResendSlots]{};
  size_t     resendCursor_ = 0;

  static constexpr uint32_t kHelloIntervalMs = 2000;
  // Follows the root VERSION file (injected as LAMP_FW_* by inject_version.py).
  static constexpr uint32_t kWispVersion     = FIRMWARE_VERSION;
};

}  // namespace wisp
