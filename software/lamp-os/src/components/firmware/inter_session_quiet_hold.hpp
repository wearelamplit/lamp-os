#pragma once

#include <cstdint>

namespace lamp {

// An OTA session's inter-session quiet hold, kept by the firmware distributor
// and receiver. During a fleet OTA wave sessions follow each other; holding
// quiet for kHoldMs after a session ends lets the next one arm inside the hold
// and inherit its quiet, so the indicator stays up instead of flickering back
// to normal between sessions. The owner serializes access.
struct InterSessionQuietHold {
  static constexpr uint32_t kHoldMs = 10000;

  bool held = false;     // this distributor owns one ota_quiet_mode count
  bool pending = false;  // a session has ended and the hold's exit is pending
  uint32_t endedAtMs = 0;
  bool endedDone = false;

  void sessionEnteredQuiet() {
    held = true;
    pending = false;
  }

  // Inert unless held: a session that never entered quiet leaves nothing to hold.
  void sessionEnded(uint32_t nowMs, bool done) {
    pending = true;
    endedAtMs = nowMs;
    endedDone = done;
  }

  void offerArmed() { pending = false; }

  bool dueToExit(uint32_t nowMs) const {
    return held && pending && nowMs - endedAtMs >= kHoldMs;
  }

  // Call after the matching exitQuiet.
  void release() {
    held = false;
    pending = false;
  }

  // True while idle inside a hold armed by a session that ended Done: the only
  // hold a new offer may arm inside. A failed session's hold just expires, so a
  // crowd of peers that never accept cannot keep the lamp quiet indefinitely.
  bool inheritable() const { return held && pending && endedDone; }
};

}  // namespace lamp
