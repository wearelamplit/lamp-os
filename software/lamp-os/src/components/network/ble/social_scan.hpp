#pragma once

#include <cstdint>

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
  }

  bool windowOpen() const { return windowOpen_; }

  // Advance one step. `active` folds the app flag, a live GATT client, and the
  // absence of an OTA radio pause. `lastWriteMs` is the last BLE-write stamp
  // for app-idle gating. Returns the scan action the caller applies.
  SocialScanAction step(uint32_t now, bool active, uint32_t lastWriteMs,
                        const SocialScanTuning& t) {
    const bool want = active && (now - sessionStartMs_ < t.maxSessionMs);
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
    if (now - lastWriteMs < t.idleGuardMs) return SocialScanAction::None;
    windowOpen_    = true;
    windowStartMs_ = now;
    return SocialScanAction::OpenWindow;
  }

 private:
  bool     windowOpen_     = false;
  uint32_t windowStartMs_  = 0;
  uint32_t windowEndMs_    = 0;
  uint32_t sessionStartMs_ = 0;
};

}  // namespace ble_control
