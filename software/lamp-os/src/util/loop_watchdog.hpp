#pragma once

#include <cstdint>

#include "util/high_water.hpp"

// The Arduino loop task under the ESP-IDF task watchdog: a loop task that
// stops feeding for the watchdog's timeout panics, and the crash record
// shows it as a task-watchdog reset. See docs/dev/embedded-heap.md "Reading a
// crash record".
namespace lamp::loop_watchdog {

// Subscribes the calling task. Call once, from the loop task, at the end of
// setup; setup itself runs unwatched.
void begin();

// Resets the loop task's watchdog. Called before every loop iteration, and
// between the steps of any loop-task operation that can block for seconds.
// A no-op on any other task and before begin().
void feed();

// True when a feed from `current` resets the watchdog: begin() has
// subscribed a task, and `current` is that task.
inline bool feedsFrom(const void* current, const void* subscribed) {
  return subscribed != nullptr && current == subscribed;
}

// Longest span between consecutive feeds, held for report until taken, so a
// new longest refused by a rate limit still prints later. Not thread-safe.
class FeedGap {
 public:
  void start(uint32_t nowMs) { lastFeedMs_ = nowMs; }
  void feed(uint32_t nowMs);
  uint32_t longestMs() const { return longest_.peek(); }
  bool reportDue() const { return unreported_; }
  // The longest span, marked reported.
  uint32_t takeReport();

 private:
  uint32_t lastFeedMs_ = 0;
  HighWaterMark longest_;
  bool unreported_ = false;
};

}  // namespace lamp::loop_watchdog
