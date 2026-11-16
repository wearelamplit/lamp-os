#pragma once

#include <atomic>
#include <cstdint>

#include <lampos/clock.hpp>

namespace webapp {

// Boot-window expiry decision, inline so it's unit-testable without WiFi.
// `neverExpire` short-circuits (apBootMinutes==0 keeps the AP up); the finite
// path is millis()-wrap-safe via signed difference.
inline bool webappShouldTeardown(uint32_t now, uint32_t deadlineMs,
                                 bool neverExpire) {
  return !neverExpire && static_cast<int32_t>(now - deadlineMs) >= 0;
}

// BLE was torn down for a serve and the session has gone idle (HTTP/ws
// deadline elapsed). Ignores station count + never-expire so a closed
// browser that stays associated still gets BLE back via reboot.
inline bool webappShouldRestoreBle(uint32_t now, uint32_t deadlineMs,
                                   bool bleDown) {
  return bleDown && static_cast<int32_t>(now - deadlineMs) >= 0;
}

// `deadlineMs` once reached, moved up to `now` so it keeps reading as reached;
// a passed deadline left over 24.8 days would read as weeks ahead. Unchanged
// while still ahead. The owner applies it at least once per 24.8 days.
inline uint32_t holdPassedDeadline(uint32_t now, uint32_t deadlineMs) {
  return static_cast<int32_t>(now - deadlineMs) >= 0 ? now : deadlineMs;
}

// Raises `deadline`, whose value the caller loaded as `seen`, to `idleMs`
// when that is later. A write that lands after the load is never lowered.
inline void raiseDeadline(std::atomic<uint32_t>& deadline, uint32_t idleMs, uint32_t seen) {
  while (lampos::isLater(idleMs, seen) && !deadline.compare_exchange_weak(seen, idleMs)) {
  }
}

// holdPassedDeadline applied to `deadline`, whose value the caller loaded as
// `seen`; a raise that lands after that load wins.
inline void holdPassedDeadline(std::atomic<uint32_t>& deadline, uint32_t now, uint32_t seen) {
  const uint32_t held = holdPassedDeadline(now, seen);
  if (held != seen) deadline.compare_exchange_strong(seen, held);
}

// The longest an armed reboot waits on OTA quiet mode; a transfer holding it
// longer is stuck, and the reboot goes ahead.
constexpr uint32_t kRebootOtaWaitMaxMs = 10u * 60u * 1000u;

// An armed reboot is due once its time passes, whether or not the AP is
// still up, and waits while an OTA holds quiet mode, up to
// kRebootOtaWaitMaxMs past its time. A time of 0 is a time like any other.
inline bool webappRebootDue(uint32_t now, bool armed, uint32_t rebootAtMs, bool otaQuiet) {
  if (!armed) return false;
  const int32_t late = static_cast<int32_t>(now - rebootAtMs);
  return late >= 0 && (!otaQuiet || late >= static_cast<int32_t>(kRebootOtaWaitMaxMs));
}

// Whether the softAP should come back when an OTA radio pause ends: a served
// page (the server is up) always gets it back, so its station can return and
// the session's BLE-restore reboot can run; otherwise only while the webapp
// would still keep it up.
inline bool apWantedAfterOta(uint32_t now, uint32_t deadlineMs, bool neverExpire,
                             bool serverUp) {
  return serverUp || !webappShouldTeardown(now, deadlineMs, neverExpire);
}

// Remembers whether an OTA radio pause took the softAP down, so the AP comes
// back after it and an AP that was already down stays down.
class OtaApFollower {
 public:
  // On a pause: true when it should take the AP down.
  bool stop(bool radioPaused, bool apUp) {
    if (!radioPaused || !apUp) return false;
    stoppedForOta_ = true;
    return true;
  }

  // True once, after the pause that took the AP down has ended, when the AP
  // is still wanted (apWantedAfterOta).
  bool restore(bool radioPaused, bool apWanted) {
    if (radioPaused || !stoppedForOta_) return false;
    stoppedForOta_ = false;
    return apWanted;
  }

 private:
  bool stoppedForOta_ = false;
};

}  // namespace webapp
