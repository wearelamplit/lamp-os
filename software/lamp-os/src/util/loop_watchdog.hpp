#pragma once

#include <cstdint>

#include "util/high_water.hpp"

// The Arduino loop task under the ESP-IDF task watchdog: a loop task that
// stops feeding for the watchdog's timeout panics, and the crash record
// shows it as a task-watchdog reset. See docs/dev/embedded-heap.md "Reading a
// crash record".
namespace lamp::loop_watchdog {

// Subscribes the calling task. Call once, from the loop task, at the end of
// setup; setup itself runs unwatched. Skips subscribing after
// kResetStreakLimit task-watchdog resets in a row, so a loop that stalls on
// every boot freezes instead of resetting forever.
void begin();

// Resets the loop task's watchdog. Called between the steps of any loop-task
// operation that can block for seconds. A no-op on any other task and before
// begin().
void feed();

// feed(), and marks the start of a loop iteration. First thing in loop().
void startIteration();

constexpr uint8_t kResetStreakLimit = 3;
// A boot that runs this long ends the streak.
constexpr uint32_t kStableUptimeMs = 60000;

// This boot's count of task-watchdog resets in a row. `prevStreak` is what
// the previous boot left, zeroed once it reached kStableUptimeMs; `stateValid`
// is false when that state did not survive the reset (a power cycle).
constexpr uint8_t bootStreak(bool stateValid, uint8_t prevStreak,
                             bool taskWdtReset) {
  return !stateValid || !taskWdtReset ? 0
         : prevStreak < kResetStreakLimit ? static_cast<uint8_t>(prevStreak + 1)
                                          : kResetStreakLimit;
}

constexpr bool watchesBoot(uint8_t streak) { return streak < kResetStreakLimit; }

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

}  // namespace lamp::loop_watchdog
