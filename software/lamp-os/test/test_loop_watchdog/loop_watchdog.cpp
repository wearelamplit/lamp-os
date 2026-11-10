// Pins the loop watchdog's bookkeeping: the longest span between marks (feeds,
// or loop iteration starts) across the millis() wrap, a new longest held for
// report until a line is admitted, the feed gate that keeps every other task
// off the watchdog, and the reset streak that stops a boot loop.

#include <unity.h>
#include <cstdint>

#include "util/loop_watchdog.hpp"
#include "../../src/util/log_rate_limiter.cpp"
#include "../../src/util/loop_watchdog.cpp"

using lamp::loop_watchdog::BootGuard;
using lamp::loop_watchdog::bootStreak;
using lamp::loop_watchdog::LongestGap;
using lamp::loop_watchdog::feedsFrom;
using lamp::LogRateLimiter;
using lamp::loop_watchdog::kResetStreakLimit;
using lamp::loop_watchdog::kStableUptimeMs;
using lamp::loop_watchdog::markIteration;
using lamp::loop_watchdog::reachedStable;
using lamp::loop_watchdog::takeDueReport;
using lamp::loop_watchdog::watchesBoot;

void setUp(void) {}
void tearDown(void) {}

void test_longest_is_the_widest_span_not_the_latest() {
  LongestGap g;
  g.start(10000);
  g.mark(10016);
  g.mark(10916);  // 900
  g.mark(10950);  // 34
  TEST_ASSERT_EQUAL_UINT32(900, g.longestMs());
}

void test_report_due_only_when_the_longest_rises() {
  LongestGap g;
  g.start(2000);
  g.mark(2040);  // 40, first span
  TEST_ASSERT_TRUE(g.reportDue());
  TEST_ASSERT_EQUAL_UINT32(40, g.takeReport());
  TEST_ASSERT_FALSE(g.reportDue());
  g.mark(2070);  // 30
  TEST_ASSERT_FALSE(g.reportDue());
  g.mark(2110);  // 40, ties the longest
  TEST_ASSERT_FALSE(g.reportDue());
  g.mark(2151);  // 41
  TEST_ASSERT_TRUE(g.reportDue());
}

void test_untaken_report_survives_later_shorter_spans() {
  LongestGap g;
  g.start(7000);
  g.mark(7300);  // 300, held while a rate limit refuses the print
  g.mark(7310);
  g.mark(7330);
  TEST_ASSERT_TRUE(g.reportDue());
  TEST_ASSERT_EQUAL_UINT32(300, g.takeReport());
  TEST_ASSERT_FALSE(g.reportDue());
}

void test_span_counts_from_start_not_from_zero() {
  LongestGap g;
  g.start(50000);
  g.mark(50250);
  TEST_ASSERT_EQUAL_UINT32(250, g.longestMs());
}

void test_span_across_millis_wrap() {
  LongestGap g;
  g.start(UINT32_MAX - 99);
  g.mark(UINT32_MAX - 49);  // 50
  g.mark(1150);             // 1200, across the wrap
  TEST_ASSERT_EQUAL_UINT32(1200, g.longestMs());
}

void test_feeds_only_from_the_subscribed_task() {
  int loopTask = 0;
  int otherTask = 0;
  TEST_ASSERT_TRUE(feedsFrom(&loopTask, &loopTask));
  TEST_ASSERT_FALSE(feedsFrom(&otherTask, &loopTask));
  // Before begin() nothing is subscribed, so no task feeds.
  TEST_ASSERT_FALSE(feedsFrom(&loopTask, nullptr));
  TEST_ASSERT_FALSE(feedsFrom(nullptr, nullptr));
}

void test_consecutive_watchdog_resets_stop_the_watch_at_the_limit() {
  uint8_t streak = 0;
  for (uint8_t reset = 1; reset < kResetStreakLimit; ++reset) {
    streak = bootStreak(true, streak, false, true);
    TEST_ASSERT_EQUAL_UINT8(reset, streak);
    TEST_ASSERT_TRUE(watchesBoot(streak));
  }
  streak = bootStreak(true, streak, false, true);
  TEST_ASSERT_FALSE(watchesBoot(streak));
  // Further watchdog resets hold it there without overflowing.
  for (int i = 0; i < 300; ++i) streak = bootStreak(true, streak, false, true);
  TEST_ASSERT_EQUAL_UINT8(kResetStreakLimit, streak);
  TEST_ASSERT_FALSE(watchesBoot(streak));
}

void test_any_other_reset_rearms() {
  TEST_ASSERT_EQUAL_UINT8(0, bootStreak(true, kResetStreakLimit, false, false));
  TEST_ASSERT_TRUE(
      watchesBoot(bootStreak(true, kResetStreakLimit, false, false)));
}

void test_lost_state_rearms_even_after_a_watchdog_reset() {
  // RTC state that fails its magic check counts as no streak.
  TEST_ASSERT_EQUAL_UINT8(0, bootStreak(false, kResetStreakLimit, false, true));
  TEST_ASSERT_EQUAL_UINT8(0, bootStreak(false, 200, false, true));
}

void test_a_late_watchdog_reset_does_not_count() {
  // A stall after the boot reached kStableUptimeMs, then two early ones.
  uint8_t streak = bootStreak(true, 0, true, true);
  TEST_ASSERT_EQUAL_UINT8(0, streak);
  streak = bootStreak(true, streak, false, true);
  streak = bootStreak(true, streak, false, true);
  TEST_ASSERT_EQUAL_UINT8(2, streak);
  TEST_ASSERT_TRUE(watchesBoot(streak));
  // A late one mid-streak ends it too.
  TEST_ASSERT_EQUAL_UINT8(0, bootStreak(true, 2, true, true));
}

void test_stable_from_the_threshold() {
  TEST_ASSERT_FALSE(reachedStable(kStableUptimeMs - 1));
  TEST_ASSERT_TRUE(reachedStable(kStableUptimeMs));
}

void test_refused_report_stays_due_until_a_line_is_admitted() {
  LongestGap g;
  LogRateLimiter log(1000, 1);
  uint32_t longest = 0;
  g.start(10000);
  g.mark(10100);  // 100
  TEST_ASSERT_TRUE(takeDueReport(g, log, 10100, longest));
  TEST_ASSERT_EQUAL_UINT32(100, longest);
  g.mark(10400);  // 300, refused inside the limiter's second
  TEST_ASSERT_FALSE(takeDueReport(g, log, 10400, longest));
  TEST_ASSERT_TRUE(g.reportDue());
  g.mark(11200);  // 800
  g.mark(11210);
  TEST_ASSERT_TRUE(takeDueReport(g, log, 11210, longest));
  TEST_ASSERT_EQUAL_UINT32(800, longest);
  TEST_ASSERT_FALSE(g.reportDue());
  // With nothing due, no line prints and the limiter's window stays unspent.
  TEST_ASSERT_FALSE(takeDueReport(g, log, 20000, longest));
  g.mark(20500);
  TEST_ASSERT_TRUE(takeDueReport(g, log, 20500, longest));
}

void test_guard_trips_on_the_third_early_reset_in_a_row() {
  BootGuard g{};
  TEST_ASSERT_TRUE(g.atBoot(false, false));  // first boot, RTC unreadable
  g.markUptime(5000);
  TEST_ASSERT_TRUE(g.atBoot(true, true));
  g.markUptime(kStableUptimeMs - 1);
  TEST_ASSERT_TRUE(g.atBoot(true, true));
  g.markUptime(12000);
  TEST_ASSERT_FALSE(g.atBoot(true, true));
}

void test_a_boot_that_ran_a_minute_breaks_the_streak() {
  BootGuard g{};
  g.atBoot(false, false);
  g.atBoot(true, true);
  g.atBoot(true, true);
  g.markUptime(kStableUptimeMs);
  // Its later watchdog reset scores 0, and the flag does not carry past it,
  // so three more early resets trip the guard.
  TEST_ASSERT_TRUE(g.atBoot(true, true));
  TEST_ASSERT_TRUE(g.atBoot(true, true));
  TEST_ASSERT_TRUE(g.atBoot(true, true));
  TEST_ASSERT_FALSE(g.atBoot(true, true));
}

void test_an_unwatched_boot_that_runs_a_minute_rearms_after_its_reset() {
  BootGuard g{};
  g.atBoot(false, false);
  g.atBoot(true, true);
  g.atBoot(true, true);
  TEST_ASSERT_FALSE(g.atBoot(true, true));
  // Unwatched, the loop keeps iterating; core 0's idle task trips the
  // watchdog ten minutes in.
  g.markUptime(600000);
  TEST_ASSERT_TRUE(g.atBoot(true, true));
}

void test_an_unwatched_iteration_still_marks_the_minute() {
  BootGuard g{};
  g.atBoot(false, false);
  g.atBoot(true, true);
  g.atBoot(true, true);
  TEST_ASSERT_FALSE(g.atBoot(true, true));
  int loopTask = 0;
  // Tripped, so begin() subscribed nothing and the iteration does not feed.
  TEST_ASSERT_FALSE(markIteration(g, kStableUptimeMs, &loopTask, nullptr));
  TEST_ASSERT_TRUE(g.atBoot(true, true));
}

void test_a_watched_iteration_feeds_only_from_the_loop_task() {
  BootGuard g{};
  g.atBoot(false, false);
  int loopTask = 0;
  int otherTask = 0;
  TEST_ASSERT_TRUE(markIteration(g, 1000, &loopTask, &loopTask));
  TEST_ASSERT_FALSE(markIteration(g, 1000, &otherTask, &loopTask));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_longest_is_the_widest_span_not_the_latest);
  RUN_TEST(test_report_due_only_when_the_longest_rises);
  RUN_TEST(test_untaken_report_survives_later_shorter_spans);
  RUN_TEST(test_span_counts_from_start_not_from_zero);
  RUN_TEST(test_span_across_millis_wrap);
  RUN_TEST(test_feeds_only_from_the_subscribed_task);
  RUN_TEST(test_consecutive_watchdog_resets_stop_the_watch_at_the_limit);
  RUN_TEST(test_any_other_reset_rearms);
  RUN_TEST(test_lost_state_rearms_even_after_a_watchdog_reset);
  RUN_TEST(test_a_late_watchdog_reset_does_not_count);
  RUN_TEST(test_stable_from_the_threshold);
  RUN_TEST(test_refused_report_stays_due_until_a_line_is_admitted);
  RUN_TEST(test_guard_trips_on_the_third_early_reset_in_a_row);
  RUN_TEST(test_a_boot_that_ran_a_minute_breaks_the_streak);
  RUN_TEST(test_an_unwatched_boot_that_runs_a_minute_rearms_after_its_reset);
  RUN_TEST(test_an_unwatched_iteration_still_marks_the_minute);
  RUN_TEST(test_a_watched_iteration_feeds_only_from_the_loop_task);
  return UNITY_END();
}
