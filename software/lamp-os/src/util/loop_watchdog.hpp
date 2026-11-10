#pragma once

#include <cstdint>

#include "util/high_water.hpp"
#include "util/log_rate_limiter.hpp"

// The Arduino loop task under the ESP-IDF task watchdog: a loop task that
// stops feeding for the watchdog's timeout panics, and the crash record
// shows it as a task-watchdog reset. See docs/dev/embedded-heap.md "Reading a
// crash record".
namespace lamp::loop_watchdog {

// Subscribes the calling task. Call once, from the loop task, at the end of
// setup; setup itself runs unwatched. Skips subscribing after
// kResetStreakLimit task-watchdog resets in a row, leaving the loop unwatched
// until a reset other than the task watchdog's.
void begin();

// Resets the loop task's watchdog. Called between the steps of any loop-task
// operation that can block for seconds. A no-op on any other task and before
// begin().
void feed();

// Marks the start of a loop iteration, then feed(). First thing in loop(),
// whether or not the loop is watched.
void startIteration();

constexpr uint8_t kResetStreakLimit = 3;
// A boot whose loop starts an iteration this far into uptime ends the streak.
constexpr uint32_t kStableUptimeMs = 60000;

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

constexpr bool watchesBoot(uint8_t streak) {
  return streak < kResetStreakLimit;
}

// The boot-loop guard's state, kept across resets in RTC memory.
struct BootGuard {
  uint8_t streak;
  uint8_t stableSeen;
  // Folds in this boot's reset; true when the loop is watched this boot.
  bool atBoot(bool stateValid, bool taskWdtReset);
  // Each loop iteration, watched or not.
  void markUptime(uint32_t uptimeMs);
};

// True when a feed from `current` resets the watchdog: begin() has
// subscribed a task, and `current` is that task.
inline bool feedsFrom(const void* current, const void* subscribed) {
  return subscribed != nullptr && current == subscribed;
}

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
bool takeDueReport(LongestGap& gap, LogRateLimiter& log, uint32_t nowMs,
                   uint32_t& longestMs);

}  // namespace lamp::loop_watchdog
