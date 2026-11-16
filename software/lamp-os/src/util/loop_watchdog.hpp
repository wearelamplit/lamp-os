#pragma once

#include <cstdint>

#include "util/high_water.hpp"
#include "util/log_rate_limiter.hpp"

// The Arduino loop task under the ESP-IDF task watchdog, from the start of
// setup. A loop task that stops feeding for the watchdog's timeout panics, and
// the crash record shows it as a task-watchdog reset. This is the one owner of
// the task watchdog's configuration. See docs/dev/embedded-heap.md "Reading a
// crash record".
namespace lamp::loop_watchdog {

constexpr uint8_t kResetStreakLimit = 3;
// Watchdog resets in a row past kResetStreakLimit, each from a backoff boot
// that outlived its window, that each double the window, so a stall that
// recurs later in every boot resets ever less often.
constexpr uint8_t kBackoffDoublings = 4;
constexpr uint8_t kStreakCap = kResetStreakLimit + kBackoffDoublings;
// The first backoff boot's window: its long timeout holds until the loop
// starts an iteration this far into uptime.
constexpr uint32_t kFirstBackoffWindowMs = 60000;
// Longest one setup stage may run between feeds.
constexpr uint32_t kSetupTimeoutMs = 30000;
// The IDF default (CONFIG_ESP_TASK_WDT_TIMEOUT_S), held for every loop iteration.
constexpr uint32_t kLoopTimeoutMs = 5000;
// Held while a firmware or filesystem image is received.
constexpr uint32_t kWideTimeoutMs = 30000;
// Held through a backoff boot until its loop runs to its backoff window.
constexpr uint32_t kBackoffTimeoutMs = 300000;
constexpr uint32_t kSubscribeRetryMs = 1000;

// Spaces subscribe attempts kSubscribeRetryMs apart, across the millis() wrap.
class RetryClock {
 public:
  bool due(uint32_t nowMs) const { return nowMs - lastTryMs_ >= kSubscribeRetryMs; }
  void tried(uint32_t nowMs) { lastTryMs_ = nowMs; }

 private:
  uint32_t lastTryMs_ = 0;
};

enum class Phase : uint8_t { Setup, Loop };

constexpr uint32_t timeoutMs(Phase phase, bool wide, bool backoff) {
  const uint32_t base = backoff                ? kBackoffTimeoutMs
                        : phase == Phase::Setup ? kSetupTimeoutMs
                                                : kLoopTimeoutMs;
  return wide && base < kWideTimeoutMs ? kWideTimeoutMs : base;
}

// The idle tasks the task watchdog checks, as esp_task_wdt_config_t's mask.
constexpr uint32_t idleCoreMask(bool checkCpu0, bool checkCpu1) {
  return (checkCpu0 ? 1u : 0u) | (checkCpu1 ? 2u : 0u);
}

// Call once, from the loop task, right after Serial.begin. Subscribes it at the
// setup timeout, or at kBackoffTimeoutMs in a backoff boot.
void beginSetup();

// Call once, from the loop task, at the end of setup. Sets the loop timeout,
// unless the boot is still backing off.
void beginLoop();

// Resets the loop task's watchdog. Called between the steps of any loop-task
// operation that can block for seconds. A no-op on any other task. On the loop
// task before it is subscribed, retries a failed subscribe at most once each
// kSubscribeRetryMs.
void feed();

// Marks the start of a loop iteration, then feed(). First thing in loop().
void startIteration();

// Holds the timeout at kWideTimeoutMs until restore(). Loop task only.
void widen();
void restore();

// How far into uptime a backoff boot at `streak` holds kBackoffTimeoutMs.
constexpr uint32_t backoffWindowMs(uint8_t streak) {
  return streak <= kResetStreakLimit
             ? kFirstBackoffWindowMs
             : kFirstBackoffWindowMs << ((streak < kStreakCap ? streak : kStreakCap) -
                                         kResetStreakLimit);
}

// A boot whose loop starts an iteration this far into uptime ends the streak.
// Past the longest backoff window, so a stall soon after it keeps the streak.
constexpr uint32_t kSettledUptimeMs = 2 * backoffWindowMs(kStreakCap);

constexpr bool reachedSettled(uint32_t uptimeMs) {
  return uptimeMs >= kSettledUptimeMs;
}

// How far a boot's loop ran: to the window its streak gives, then to
// kSettledUptimeMs.
enum class Reached : uint8_t { None, Window, Settled };

// This boot's count of task-watchdog resets in a row, each from a boot whose
// loop never started an iteration at kSettledUptimeMs. Past kResetStreakLimit
// it grows only on a reset of a boot whose loop ran to its window, up to
// kStreakCap.
// `prevStreak` and `prevReached` are what the previous boot left; `stateValid`
// is false when they did not survive the reset.
constexpr uint8_t bootStreak(bool stateValid, uint8_t prevStreak, Reached prevReached,
                             bool taskWdtReset) {
  return !stateValid || !taskWdtReset || prevReached == Reached::Settled ? 0
         : prevStreak >= kStreakCap ? kStreakCap
         : prevStreak < kResetStreakLimit || prevReached == Reached::Window
             ? static_cast<uint8_t>(prevStreak + 1)
             : prevStreak;
}

// Backoff holds kBackoffTimeoutMs until the loop runs to the boot's backoff
// window, so a boot that stalls every time resets slowly instead of looping
// fast.
enum class Mode : uint8_t { Normal, Backoff };

constexpr Mode modeFor(uint8_t streak) {
  return streak < kResetStreakLimit ? Mode::Normal : Mode::Backoff;
}

// The boot-loop guard's state, kept across resets in RTC memory.
struct BootGuard {
  uint8_t streak;
  Reached reached;
  // Folds in this boot's reset.
  Mode atBoot(bool stateValid, bool taskWdtReset);
  // Each loop iteration, watched or not.
  void markUptime(uint32_t uptimeMs);
  // True when a loop iteration at `uptimeMs` in a backoff boot returns it to
  // the normal timeouts.
  constexpr bool rearmDue(Mode mode, uint32_t uptimeMs) const {
    return mode == Mode::Backoff && uptimeMs >= backoffWindowMs(streak);
  }
};

// True when a feed from `current` resets the watchdog. `subscribed` is the
// loop task once subscribed, else null, and `current` must be it.
inline bool feedsFrom(const void* current, const void* subscribed) {
  return subscribed != nullptr && current == subscribed;
}

// The start of a loop iteration at `uptimeMs`, marked on `guard` whether or not
// the loop is watched. True when the iteration feeds the watchdog.
bool markIteration(BootGuard& guard, uint32_t uptimeMs, const void* current,
                   const void* subscribed);

// Longest span between consecutive marks, held for report until taken, so a
// new longest refused by a rate limit still prints later. Not thread-safe.
class LongestGap {
 public:
  void start(uint32_t nowMs) { lastMarkMs_ = nowMs; }
  void mark(uint32_t nowMs);
  uint32_t longestMs() const { return longest_.peek(); }
  bool reportDue() const { return unreported_; }
  // The longest span, marked reported.
  uint32_t takeReport();

 private:
  uint32_t lastMarkMs_ = 0;
  HighWaterMark longest_;
  bool unreported_ = false;
};

// Takes the gap's report into `longestMs` when one is due and `log` admits a
// line at nowMs. A refused report stays due.
bool takeDueReport(LongestGap& gap, LogRateLimiter& log, uint64_t nowMs,
                   uint32_t& longestMs);

}  // namespace lamp::loop_watchdog
