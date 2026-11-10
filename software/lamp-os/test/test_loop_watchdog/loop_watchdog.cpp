// Pins the loop watchdog's feed bookkeeping: the longest span between feeds,
// reported once per new high, across the millis() wrap.

#include <unity.h>
#include <cstdint>

#include "util/loop_watchdog.hpp"
#include "../../src/util/loop_watchdog.cpp"

using lamp::loop_watchdog::FeedGap;

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

void test_new_high_reported_only_when_it_rises() {
  FeedGap g;
  g.start(2000);
  TEST_ASSERT_TRUE(g.feed(2040));    // 40, first span
  TEST_ASSERT_FALSE(g.feed(2070));   // 30
  TEST_ASSERT_FALSE(g.feed(2110));   // 40, ties the high
  TEST_ASSERT_TRUE(g.feed(2151));    // 41
  TEST_ASSERT_FALSE(g.feed(2152));
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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_longest_is_the_widest_span_not_the_latest);
  RUN_TEST(test_new_high_reported_only_when_it_rises);
  RUN_TEST(test_span_counts_from_start_not_from_zero);
  RUN_TEST(test_span_across_millis_wrap);
  return UNITY_END();
}
