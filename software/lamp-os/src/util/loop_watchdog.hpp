#pragma once

#include <atomic>
#include <cstdint>

#include "util/high_water.hpp"
#include "util/log_rate_limiter.hpp"

// The Arduino loop task under the ESP-IDF task watchdog, from the start of
// setup: a loop task that stops feeding for the watchdog's timeout panics, and
// the crash record shows it as a task-watchdog reset. The task watchdog's
// configuration has this one owner. See docs/dev/embedded-heap.md "Reading a
// crash record".
namespace lamp::loop_watchdog {

constexpr uint8_t kResetStreakLimit = 3;
// A boot whose loop starts an iteration this far into uptime ends the streak.
constexpr uint32_t kStableUptimeMs = 60000;
// Longest one setup stage may run between feeds.
constexpr uint32_t kSetupTimeoutMs = 30000;
// The IDF default (CONFIG_ESP_TASK_WDT_TIMEOUT_S), held for every loop iteration.
constexpr uint32_t kLoopTimeoutMs = 5000;
// Held while a firmware or filesystem image is received.
constexpr uint32_t kWideTimeoutMs = 30000;
// How long a backoff boot's loop task stays unsubscribed, from setup start.
constexpr uint32_t kBackoffMs = 300000;

enum class Phase : uint8_t { Setup, Loop };

constexpr uint32_t timeoutMs(Phase phase, bool wide) {
  const uint32_t base = phase == Phase::Setup ? kSetupTimeoutMs : kLoopTimeoutMs;
  return wide && base < kWideTimeoutMs ? kWideTimeoutMs : base;
}

// The idle tasks the task watchdog checks, as esp_task_wdt_config_t's mask.
constexpr uint32_t idleCoreMask(bool checkCpu0, bool checkCpu1) {
  return (checkCpu0 ? 1u : 0u) | (checkCpu1 ? 2u : 0u);
}

// Call once, from the loop task, right after Serial.begin. Subscribes it at the
// setup timeout, or, in a backoff boot, arms a timer that subscribes it
// kBackoffMs later.
void beginSetup();

// Call once, from the loop task, at the end of setup: the loop timeout.
void beginLoop();

// Resets the loop task's watchdog. Called between the steps of any loop-task
// operation that can block for seconds. A no-op on any other task and while
// the loop task is unsubscribed.
void feed();

// Marks the start of a loop iteration, then feed(). First thing in loop().
void startIteration();

// Holds the timeout at kWideTimeoutMs until restore(). Loop task only.
void widen();
void restore();

constexpr bool reachedStable(uint32_t uptimeMs) {
  return uptimeMs >= kStableUptimeMs;
}

// This boot's count of task-watchdog resets in a row, each from a boot whose
// loop never started an iteration at kStableUptimeMs. `prevStreak` and
// `prevReachedStable` are what the previous boot left; `stateValid` is false
// when they did not survive the reset.
constexpr uint8_t bootStreak(bool stateValid, uint8_t prevStreak,
                             bool prevReachedStable, bool taskWdtReset) {
  return !stateValid || !taskWdtReset || prevReachedStable ? 0
         : prevStreak < kResetStreakLimit ? static_cast<uint8_t>(prevStreak + 1)
                                          : kResetStreakLimit;
}

// Normal: watched from setup start. Backoff: unsubscribed for kBackoffMs, or
// until the loop runs to kStableUptimeMs, so a boot that stalls every time
// resets slowly instead of looping fast.
enum class Mode : uint8_t { Normal, Backoff };

constexpr Mode modeFor(uint8_t streak) {
  return streak < kResetStreakLimit ? Mode::Normal : Mode::Backoff;
}

// True when a backoff boot's loop iteration at `uptimeMs` subscribes the loop
// task ahead of the backoff timer.
constexpr bool rearmDue(Mode mode, uint32_t uptimeMs) {
  return mode == Mode::Backoff && reachedStable(uptimeMs);
}

// The boot-loop guard's state, kept across resets in RTC memory.
struct BootGuard {
  uint8_t streak;
  uint8_t stableSeen;
  // Folds in this boot's reset.
  Mode atBoot(bool stateValid, bool taskWdtReset);
  // Each loop iteration, watched or not.
  void markUptime(uint32_t uptimeMs);
};

// True when a feed from `current` resets the watchdog: the loop task is
// subscribed, and `current` is that task.
inline bool feedsFrom(const void* current, const void* subscribed) {
  return subscribed != nullptr && current == subscribed;
}

// The start of a loop iteration at `uptimeMs`, marked on `guard` whether or not
// the loop is watched. True when the iteration feeds the watchdog.
bool markIteration(BootGuard& guard, uint32_t uptimeMs, const void* current,
                   const void* subscribed);

// Lets exactly one of several racing callers through, once.
class Once {
 public:
  bool claim() {
    bool expected = false;
    return claimed_.compare_exchange_strong(expected, true);
  }

 private:
  std::atomic<bool> claimed_{false};
};

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
