// Pins the loop watchdog's bookkeeping: the longest span between marks (feeds,
// or loop iteration starts) across the millis() wrap, a new longest held for
// report until a line is admitted, the feed gate that keeps every other task
// off the watchdog, the timeout each phase holds, and the reset streak that
// backs a boot loop off, widens the backoff while the stall recurs, and re-arms
// it.

#include <unity.h>
#include <cstdint>
#include <initializer_list>

#include "util/loop_watchdog.hpp"
#include "../../src/util/log_rate_limiter.cpp"
#include "../../src/util/loop_watchdog.cpp"

using lamp::loop_watchdog::backoffWindowMs;
using lamp::loop_watchdog::BootGuard;
using lamp::loop_watchdog::bootStreak;
using lamp::loop_watchdog::LongestGap;
using lamp::loop_watchdog::feedsFrom;
using lamp::LogRateLimiter;
using lamp::loop_watchdog::kResetStreakLimit;
using lamp::loop_watchdog::kSettledUptimeMs;
using lamp::loop_watchdog::kFirstBackoffWindowMs;
using lamp::loop_watchdog::kStreakCap;
using lamp::loop_watchdog::markIteration;
using lamp::loop_watchdog::reachedSettled;
using lamp::loop_watchdog::takeDueReport;
using lamp::loop_watchdog::idleCoreMask;
using lamp::loop_watchdog::kBackoffTimeoutMs;
using lamp::loop_watchdog::kLoopTimeoutMs;
using lamp::loop_watchdog::kSetupTimeoutMs;
using lamp::loop_watchdog::kWideTimeoutMs;
using lamp::loop_watchdog::Mode;
using lamp::loop_watchdog::modeFor;
using lamp::loop_watchdog::Phase;
using lamp::loop_watchdog::kSubscribeRetryMs;
using lamp::loop_watchdog::Reached;
using lamp::loop_watchdog::rearmDue;
using lamp::loop_watchdog::RetryClock;
using lamp::loop_watchdog::timeoutMs;

static bool watchesBoot(uint8_t streak) { return modeFor(streak) == Mode::Normal; }

static bool watchedAt(BootGuard& g, bool stateValid, bool taskWdtReset) {
  return g.atBoot(stateValid, taskWdtReset) == Mode::Normal;
}

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
  // Before the loop task is subscribed, no task feeds.
  TEST_ASSERT_FALSE(feedsFrom(&loopTask, nullptr));
  TEST_ASSERT_FALSE(feedsFrom(nullptr, nullptr));
}

void test_consecutive_watchdog_resets_back_off_at_the_limit() {
  for (const Reached reached : {Reached::None, Reached::Window}) {
    uint8_t streak = 0;
    for (uint8_t reset = 1; reset < kResetStreakLimit; ++reset) {
      streak = bootStreak(true, streak, reached, true);
      TEST_ASSERT_EQUAL_UINT8(reset, streak);
      TEST_ASSERT_TRUE(watchesBoot(streak));
    }
    streak = bootStreak(true, streak, reached, true);
    TEST_ASSERT_FALSE(watchesBoot(streak));
    // Further watchdog resets hold it in backoff without overflowing.
    for (int i = 0; i < 300; ++i) {
      streak = bootStreak(true, streak, reached, true);
      TEST_ASSERT_FALSE(watchesBoot(streak));
    }
    // It climbs past the limit only on resets after the window.
    TEST_ASSERT_EQUAL_UINT8(reached == Reached::Window ? kStreakCap : kResetStreakLimit,
                            streak);
  }
}

void test_a_reset_inside_the_window_keeps_the_streak() {
  for (uint8_t streak = kResetStreakLimit; streak <= kStreakCap; ++streak) {
    TEST_ASSERT_EQUAL_UINT8(streak, bootStreak(true, streak, Reached::None, true));
  }
  TEST_ASSERT_EQUAL_UINT8(kResetStreakLimit + 2,
                          bootStreak(true, kResetStreakLimit + 1, Reached::Window, true));
  // A streak the RTC left out of range comes back to the cap.
  TEST_ASSERT_EQUAL_UINT8(kStreakCap, bootStreak(true, 200, Reached::None, true));
  TEST_ASSERT_EQUAL_UINT8(kStreakCap, bootStreak(true, 200, Reached::Window, true));
}

void test_any_other_reset_rearms() {
  TEST_ASSERT_EQUAL_UINT8(0, bootStreak(true, kResetStreakLimit, Reached::Window, false));
  TEST_ASSERT_TRUE(
      watchesBoot(bootStreak(true, kResetStreakLimit, Reached::Window, false)));
}

void test_lost_state_rearms_even_after_a_watchdog_reset() {
  // RTC state that fails its magic check counts as no streak.
  TEST_ASSERT_EQUAL_UINT8(0, bootStreak(false, kResetStreakLimit, Reached::Window, true));
  TEST_ASSERT_EQUAL_UINT8(0, bootStreak(false, 200, Reached::None, true));
}

void test_a_settled_boots_watchdog_reset_does_not_count() {
  // A stall after the boot reached kSettledUptimeMs, then two early ones.
  uint8_t streak = bootStreak(true, 0, Reached::Settled, true);
  TEST_ASSERT_EQUAL_UINT8(0, streak);
  streak = bootStreak(true, streak, Reached::None, true);
  streak = bootStreak(true, streak, Reached::Window, true);
  TEST_ASSERT_EQUAL_UINT8(2, streak);
  TEST_ASSERT_TRUE(watchesBoot(streak));
  // A settled one mid-streak, or mid-backoff, ends it too.
  TEST_ASSERT_EQUAL_UINT8(0, bootStreak(true, 2, Reached::Settled, true));
  TEST_ASSERT_EQUAL_UINT8(0, bootStreak(true, kStreakCap, Reached::Settled, true));
}

void test_settled_from_the_threshold() {
  TEST_ASSERT_FALSE(reachedSettled(kSettledUptimeMs - 1));
  TEST_ASSERT_TRUE(reachedSettled(kSettledUptimeMs));
  // Past every backoff window, so a boot that re-arms has not yet settled.
  for (uint8_t streak = 0; streak <= kStreakCap + 1; ++streak) {
    TEST_ASSERT_TRUE(kSettledUptimeMs > backoffWindowMs(streak));
  }
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

void test_guard_backs_off_on_the_third_early_reset_in_a_row() {
  BootGuard g{};
  TEST_ASSERT_TRUE(watchedAt(g, false, false));  // first boot, RTC unreadable
  g.markUptime(5000);
  TEST_ASSERT_TRUE(watchedAt(g, true, true));
  g.markUptime(kFirstBackoffWindowMs - 1);
  TEST_ASSERT_TRUE(watchedAt(g, true, true));
  g.markUptime(12000);
  TEST_ASSERT_FALSE(watchedAt(g, true, true));
}

void test_a_boot_that_settled_breaks_the_streak() {
  BootGuard g{};
  g.atBoot(false, false);
  g.atBoot(true, true);
  g.atBoot(true, true);
  g.markUptime(kSettledUptimeMs);
  // Its later watchdog reset scores 0, and the flag does not carry past it,
  // so three more early resets trip the guard.
  TEST_ASSERT_TRUE(watchedAt(g, true, true));
  TEST_ASSERT_TRUE(watchedAt(g, true, true));
  TEST_ASSERT_TRUE(watchedAt(g, true, true));
  TEST_ASSERT_FALSE(watchedAt(g, true, true));
}

void test_a_backoff_boot_that_settles_rearms_after_its_reset() {
  BootGuard g{};
  g.atBoot(false, false);
  // Each boot outlives its window, then resets.
  for (int i = 0; i < 10; ++i) {
    g.markUptime(backoffWindowMs(g.streak));
    g.atBoot(true, true);
  }
  TEST_ASSERT_EQUAL_UINT8(kStreakCap, g.streak);
  // Backing off at the widest window, the loop settles before its reset.
  g.markUptime(kSettledUptimeMs + 1000);
  TEST_ASSERT_TRUE(watchedAt(g, true, true));
}

void test_an_unsubscribed_iteration_still_marks_the_settled_uptime() {
  BootGuard g{};
  g.atBoot(false, false);
  g.atBoot(true, true);
  g.atBoot(true, true);
  TEST_ASSERT_FALSE(watchedAt(g, true, true));
  int loopTask = 0;
  // Its subscribe failed, so the iteration does not feed, yet the uptime counts.
  TEST_ASSERT_FALSE(markIteration(g, kSettledUptimeMs, &loopTask, nullptr));
  TEST_ASSERT_TRUE(watchedAt(g, true, true));
}

void test_a_watched_iteration_feeds_and_marks_the_settled_uptime() {
  BootGuard g{};
  g.atBoot(false, false);
  int loopTask = 0;
  int otherTask = 0;
  TEST_ASSERT_TRUE(markIteration(g, kSettledUptimeMs, &loopTask, &loopTask));
  g.atBoot(true, true);
  TEST_ASSERT_EQUAL_UINT8(0, g.streak);
  TEST_ASSERT_FALSE(markIteration(g, 1000, &otherTask, &loopTask));
}

void test_each_phase_holds_its_timeout() {
  TEST_ASSERT_EQUAL_UINT32(kSetupTimeoutMs, timeoutMs(Phase::Setup, false, false));
  TEST_ASSERT_EQUAL_UINT32(kLoopTimeoutMs, timeoutMs(Phase::Loop, false, false));
  TEST_ASSERT_EQUAL_UINT32(kWideTimeoutMs, timeoutMs(Phase::Loop, true, false));
  TEST_ASSERT_TRUE(kSetupTimeoutMs > kLoopTimeoutMs);
}

void test_a_backoff_boot_holds_the_backoff_timeout_throughout() {
  for (const Phase p : {Phase::Setup, Phase::Loop}) {
    for (const bool wide : {false, true}) {
      TEST_ASSERT_EQUAL_UINT32(kBackoffTimeoutMs, timeoutMs(p, wide, true));
    }
  }
  // Long enough to outlast an early stall and the minute that re-arms, and
  // still finite.
  TEST_ASSERT_TRUE(kBackoffTimeoutMs > kFirstBackoffWindowMs);
  TEST_ASSERT_TRUE(kBackoffTimeoutMs > kWideTimeoutMs);
}

void test_widening_never_shortens_the_timeout() {
  for (const Phase p : {Phase::Setup, Phase::Loop}) {
    for (const bool backoff : {false, true}) {
      TEST_ASSERT_TRUE(timeoutMs(p, true, backoff) >= timeoutMs(p, false, backoff));
      TEST_ASSERT_TRUE(timeoutMs(p, true, backoff) >= kWideTimeoutMs);
    }
  }
}

void test_idle_mask_has_one_bit_per_checked_core() {
  TEST_ASSERT_EQUAL_UINT32(0, idleCoreMask(false, false));
  TEST_ASSERT_EQUAL_UINT32(1, idleCoreMask(true, false));
  TEST_ASSERT_EQUAL_UINT32(2, idleCoreMask(false, true));
  TEST_ASSERT_EQUAL_UINT32(3, idleCoreMask(true, true));
}

void test_a_boot_loop_stays_in_backoff_while_it_stalls_early() {
  BootGuard g{};
  g.atBoot(false, false);
  g.atBoot(true, true);
  g.atBoot(true, true);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Backoff), static_cast<int>(g.atBoot(true, true)));
  // Every further early reset stays in backoff, which still ends.
  for (int i = 0; i < 10; ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::Backoff), static_cast<int>(g.atBoot(true, true)));
  }
}

void test_first_backoff_boot_rearms_at_the_first_window() {
  BootGuard g{};
  g.atBoot(false, false);
  for (uint8_t reset = 0; reset < kResetStreakLimit; ++reset) g.atBoot(true, true);
  TEST_ASSERT_FALSE(rearmDue(Mode::Backoff, g, 0));
  TEST_ASSERT_FALSE(rearmDue(Mode::Backoff, g, kFirstBackoffWindowMs - 1));
  TEST_ASSERT_TRUE(rearmDue(Mode::Backoff, g, kFirstBackoffWindowMs));
  TEST_ASSERT_TRUE(rearmDue(Mode::Backoff, g, kBackoffTimeoutMs - 1));
  // A normal boot already holds the normal timeouts.
  for (uint8_t streak = 0; streak <= kStreakCap; ++streak) {
    const BootGuard at{streak, Reached::None};
    TEST_ASSERT_FALSE(rearmDue(Mode::Normal, at, kFirstBackoffWindowMs));
    TEST_ASSERT_FALSE(rearmDue(Mode::Normal, at, kSettledUptimeMs));
  }
}

void test_each_reset_after_the_window_doubles_it_up_to_the_cap() {
  BootGuard g{};
  g.atBoot(false, false);
  for (uint8_t reset = 0; reset < kResetStreakLimit; ++reset) g.atBoot(true, true);
  for (int boot = 0; boot < kStreakCap - kResetStreakLimit; ++boot) {
    const uint32_t windowMs = backoffWindowMs(g.streak);
    TEST_ASSERT_FALSE(rearmDue(Mode::Backoff, g, windowMs - 1));
    TEST_ASSERT_TRUE(rearmDue(Mode::Backoff, g, windowMs));
    g.markUptime(windowMs + 1000);
    g.atBoot(true, true);
    TEST_ASSERT_EQUAL_UINT32(2 * windowMs, backoffWindowMs(g.streak));
  }
  // Past the cap, and on a streak the RTC left out of range, it stays bounded.
  TEST_ASSERT_EQUAL_UINT32(backoffWindowMs(kStreakCap), backoffWindowMs(kStreakCap + 1));
  TEST_ASSERT_EQUAL_UINT32(backoffWindowMs(kStreakCap), backoffWindowMs(255));
}

// Uptime at which a boot resets when its loop stalls past the loop timeout,
// and for less than kBackoffTimeoutMs, every `periodMs` of uptime.
static uint32_t stallResetUptimeMs(const BootGuard& g, Mode mode, uint32_t periodMs) {
  uint32_t stallAtMs = periodMs;
  while (mode == Mode::Backoff && !rearmDue(mode, g, stallAtMs)) {
    stallAtMs += periodMs;
  }
  return stallAtMs;
}

void test_a_stall_that_recurs_after_the_first_minute_resets_ever_less_often() {
  // 150 s is past the first minute and never lands on a backoff window.
  constexpr uint32_t kPeriodMs = 150000;
  BootGuard g{};
  Mode mode = g.atBoot(false, false);
  uint32_t firstMs = 0;
  uint32_t prevMs = 0;
  bool backedOff = false;
  for (int boot = 0; boot < 12; ++boot) {
    const uint32_t resetMs = stallResetUptimeMs(g, mode, kPeriodMs);
    if (boot == 0) firstMs = resetMs;
    TEST_ASSERT_TRUE(resetMs >= prevMs);
    TEST_ASSERT_TRUE(resetMs <= backoffWindowMs(kStreakCap) + kPeriodMs);
    prevMs = resetMs;
    g.markUptime(resetMs);
    mode = g.atBoot(true, true);
    backedOff = backedOff || mode == Mode::Backoff;
  }
  TEST_ASSERT_TRUE(backedOff);
  // The cadence widens to the longest window and holds there.
  TEST_ASSERT_TRUE(prevMs > firstMs);
  TEST_ASSERT_TRUE(prevMs >= backoffWindowMs(kStreakCap));
}

void test_a_failed_subscribe_is_retried_once_a_second_across_the_wrap() {
  RetryClock clock;
  TEST_ASSERT_FALSE(clock.due(kSubscribeRetryMs - 1));
  TEST_ASSERT_TRUE(clock.due(kSubscribeRetryMs));
  // An attempt holds the next one off for the whole interval.
  clock.tried(41000);
  TEST_ASSERT_FALSE(clock.due(41000));
  TEST_ASSERT_FALSE(clock.due(41999));
  TEST_ASSERT_TRUE(clock.due(42000));
  clock.tried(UINT32_MAX - 100);
  TEST_ASSERT_FALSE(clock.due(UINT32_MAX - 50));
  TEST_ASSERT_FALSE(clock.due(800));
  TEST_ASSERT_TRUE(clock.due(900));
}

void test_watchdog_resets_after_the_first_minute_still_back_off() {
  BootGuard g{};
  g.atBoot(false, false);
  // Each boot's loop runs past the first minute, then stalls the watchdog.
  for (uint8_t reset = 1; reset < kResetStreakLimit; ++reset) {
    g.markUptime(kFirstBackoffWindowMs + 90000);
    TEST_ASSERT_TRUE(watchedAt(g, true, true));
  }
  g.markUptime(kFirstBackoffWindowMs + 90000);
  TEST_ASSERT_FALSE(watchedAt(g, true, true));
}

void test_a_boot_loop_that_stalls_in_the_first_minute_keeps_the_first_window() {
  BootGuard g{};
  g.atBoot(false, false);
  // Every boot stalls 20 s in, inside even the first backoff window.
  for (int boot = 0; boot < 10; ++boot) {
    g.markUptime(20000);
    g.atBoot(true, true);
  }
  TEST_ASSERT_EQUAL_UINT32(kFirstBackoffWindowMs, backoffWindowMs(g.streak));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_longest_is_the_widest_span_not_the_latest);
  RUN_TEST(test_report_due_only_when_the_longest_rises);
  RUN_TEST(test_untaken_report_survives_later_shorter_spans);
  RUN_TEST(test_span_counts_from_start_not_from_zero);
  RUN_TEST(test_span_across_millis_wrap);
  RUN_TEST(test_feeds_only_from_the_subscribed_task);
  RUN_TEST(test_consecutive_watchdog_resets_back_off_at_the_limit);
  RUN_TEST(test_a_reset_inside_the_window_keeps_the_streak);
  RUN_TEST(test_any_other_reset_rearms);
  RUN_TEST(test_lost_state_rearms_even_after_a_watchdog_reset);
  RUN_TEST(test_a_settled_boots_watchdog_reset_does_not_count);
  RUN_TEST(test_settled_from_the_threshold);
  RUN_TEST(test_refused_report_stays_due_until_a_line_is_admitted);
  RUN_TEST(test_guard_backs_off_on_the_third_early_reset_in_a_row);
  RUN_TEST(test_a_boot_that_settled_breaks_the_streak);
  RUN_TEST(test_a_backoff_boot_that_settles_rearms_after_its_reset);
  RUN_TEST(test_an_unsubscribed_iteration_still_marks_the_settled_uptime);
  RUN_TEST(test_a_watched_iteration_feeds_and_marks_the_settled_uptime);
  RUN_TEST(test_each_phase_holds_its_timeout);
  RUN_TEST(test_a_backoff_boot_holds_the_backoff_timeout_throughout);
  RUN_TEST(test_widening_never_shortens_the_timeout);
  RUN_TEST(test_idle_mask_has_one_bit_per_checked_core);
  RUN_TEST(test_a_boot_loop_stays_in_backoff_while_it_stalls_early);
  RUN_TEST(test_first_backoff_boot_rearms_at_the_first_window);
  RUN_TEST(test_each_reset_after_the_window_doubles_it_up_to_the_cap);
  RUN_TEST(test_a_stall_that_recurs_after_the_first_minute_resets_ever_less_often);
  RUN_TEST(test_a_failed_subscribe_is_retried_once_a_second_across_the_wrap);
  RUN_TEST(test_watchdog_resets_after_the_first_minute_still_back_off);
  RUN_TEST(test_a_boot_loop_that_stalls_in_the_first_minute_keeps_the_first_window);
  return UNITY_END();
}
