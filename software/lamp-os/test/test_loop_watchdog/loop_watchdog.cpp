// Pins the loop watchdog's bookkeeping: the longest span between feeds across
// the millis() wrap, a new longest held for report until taken, and the feed
// gate that keeps every other task off the watchdog.

#include <unity.h>
#include <cstdint>

#include "util/loop_watchdog.hpp"
#include "../../src/util/loop_watchdog.cpp"

using lamp::loop_watchdog::FeedGap;
using lamp::loop_watchdog::feedsFrom;

void setUp(void) {}
void tearDown(void) {}

void test_longest_is_the_widest_span_not_the_latest() {
  FeedGap g;
  g.start(10000);
  g.feed(10016);
  g.feed(10916);  // 900
  g.feed(10950);  // 34
  TEST_ASSERT_EQUAL_UINT32(900, g.longestMs());
}

void test_report_due_only_when_the_longest_rises() {
  FeedGap g;
  g.start(2000);
  g.feed(2040);  // 40, first span
  TEST_ASSERT_TRUE(g.reportDue());
  TEST_ASSERT_EQUAL_UINT32(40, g.takeReport());
  TEST_ASSERT_FALSE(g.reportDue());
  g.feed(2070);  // 30
  TEST_ASSERT_FALSE(g.reportDue());
  g.feed(2110);  // 40, ties the longest
  TEST_ASSERT_FALSE(g.reportDue());
  g.feed(2151);  // 41
  TEST_ASSERT_TRUE(g.reportDue());
}

void test_untaken_report_survives_later_shorter_spans() {
  FeedGap g;
  g.start(7000);
  g.feed(7300);  // 300, held while a rate limit refuses the print
  g.feed(7310);
  g.feed(7330);
  TEST_ASSERT_TRUE(g.reportDue());
  TEST_ASSERT_EQUAL_UINT32(300, g.takeReport());
  TEST_ASSERT_FALSE(g.reportDue());
}

void test_span_counts_from_start_not_from_zero() {
  FeedGap g;
  g.start(50000);
  g.feed(50250);
  TEST_ASSERT_EQUAL_UINT32(250, g.longestMs());
}

void test_span_across_millis_wrap() {
  FeedGap g;
  g.start(UINT32_MAX - 99);
  g.feed(UINT32_MAX - 49);  // 50
  g.feed(1150);             // 1200, across the wrap
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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_longest_is_the_widest_span_not_the_latest);
  RUN_TEST(test_report_due_only_when_the_longest_rises);
  RUN_TEST(test_untaken_report_survives_later_shorter_spans);
  RUN_TEST(test_span_counts_from_start_not_from_zero);
  RUN_TEST(test_span_across_millis_wrap);
  RUN_TEST(test_feeds_only_from_the_subscribed_task);
  return UNITY_END();
}
