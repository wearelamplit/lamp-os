#pragma once

#include <cstdint>

#include <lampos/clock.hpp>

namespace ble_control {

// Duty-cycled passive-scan gate for the app's Social/Network view. On GATT
// connect the lamp stops its central scan to protect BLE write throughput, so
// BLE-only (non-mesh) peers stop being sighted into the roster. While the app
// signals the Social view is open, this gate reopens short passive-scan
// windows so those peers keep entering the roster, held low-duty and skipped
// during recent BLE writes so the connected session's throughput holds.
//
// Pure logic, no radio dependency: ble_control::tick() drives step() on Core 1
// and applies the returned action to the NimBLE scan.
struct SocialScanTuning {
  uint32_t windowMs     = 30;      // passive-scan window length
  uint32_t gapMs        = 300;     // quiet gap between windows (~10% duty)
  uint32_t idleGuardMs  = 150;     // skip a window if a BLE write fired this recently
  uint32_t maxSessionMs = 600000;  // safety cap; scanning stops after this while armed
};

enum class SocialScanAction { None, OpenWindow, CloseWindow };

class SocialScanGate {
 public:
  // Reset the session clock. Call on the rising edge of the active signal.
  void arm(uint32_t now) {
    sessionStartMs_ = now;
    windowEndMs_    = now;
    sessionOver_    = false;
  }

  bool windowOpen() const { return windowOpen_; }

  // Advance one step. `active` folds the app flag, a live GATT client, and the
  // absence of an OTA radio pause. `lastWriteMs` is the last BLE-write stamp
  // for app-idle gating. Returns the scan action the caller applies. Call it
  // at least once per millis() wrap, active or not, so the session cap and a
  // write stamp past the idle guard stay retired.
  SocialScanAction step(uint32_t now, bool active, uint32_t lastWriteMs,
                        const SocialScanTuning& t) {
    if (lampos::msSince(now, sessionStartMs_) >= t.maxSessionMs) sessionOver_ = true;
    if (lampos::msSince(now, lastWriteMs) >= t.idleGuardMs) {
      idleWriteMs_ = lastWriteMs;
      idleWriteKnown_ = true;
    }
    const bool want = active && !sessionOver_ && (now - sessionStartMs_ < t.maxSessionMs);
    if (!want) {
      if (windowOpen_) {
        windowOpen_  = false;
        windowEndMs_ = now;
        return SocialScanAction::CloseWindow;
      }
      return SocialScanAction::None;
    }
    if (windowOpen_) {
      if (now - windowStartMs_ >= t.windowMs) {
        windowOpen_  = false;
        windowEndMs_ = now;
        return SocialScanAction::CloseWindow;
      }
      return SocialScanAction::None;
    }
    if (now - windowEndMs_ < t.gapMs) return SocialScanAction::None;
    const bool writeIdle = idleWriteKnown_ && lastWriteMs == idleWriteMs_;
    if (!writeIdle && now - lastWriteMs < t.idleGuardMs) return SocialScanAction::None;
    windowOpen_    = true;
    windowStartMs_ = now;
    return SocialScanAction::OpenWindow;
  }

 private:
  bool     windowOpen_     = false;
  uint32_t windowStartMs_  = 0;
  uint32_t windowEndMs_    = 0;
  uint32_t sessionStartMs_ = 0;
  bool     sessionOver_    = false;
  // The last write stamp seen past the idle guard: that stamp no longer holds
  // a window off, however its age reads after a wrap.
  uint32_t idleWriteMs_    = 0;
  bool     idleWriteKnown_ = false;
};

}  // namespace ble_control
