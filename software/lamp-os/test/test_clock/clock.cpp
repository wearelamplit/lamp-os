// Native tests for lampos/clock.hpp, the wrap-safe millis() comparisons shared
// by lamp and wisp. Every case straddles the 32-bit wrap at 2^32 ms.

#include <unity.h>

#include <cstdint>

#include <lampos/clock.hpp>

using lampos::isLater;
using lampos::kStampSkewMs;
using lampos::msSince;
using lampos::timeReached;

void setUp(void) {}
void tearDown(void) {}

// Armed 100 ms before the wrap with a 300 ms wait: the deadline lands 200 ms
// past it.
static constexpr uint32_t kArmedMs = UINT32_MAX - 99;
static constexpr uint32_t kDeadlineMs = kArmedMs + 300;

void test_time_reached_waits_for_a_deadline_past_the_wrap() {
  TEST_ASSERT_FALSE(timeReached(kArmedMs, kDeadlineMs));
  TEST_ASSERT_FALSE(timeReached(kArmedMs + 50, kDeadlineMs));
  TEST_ASSERT_FALSE(timeReached(kDeadlineMs - 1, kDeadlineMs));
  TEST_ASSERT_TRUE(timeReached(kDeadlineMs, kDeadlineMs));
  TEST_ASSERT_TRUE(timeReached(kDeadlineMs + 1000, kDeadlineMs));
}

void test_time_reached_holds_a_deadline_before_the_wrap_after_it() {
  const uint32_t deadline = UINT32_MAX - 40;
  TEST_ASSERT_FALSE(timeReached(deadline - 1, deadline));
  TEST_ASSERT_TRUE(timeReached(deadline, deadline));
  TEST_ASSERT_TRUE(timeReached(0x200, deadline));
}

void test_is_later_orders_stamps_across_the_wrap() {
  const uint32_t before = UINT32_MAX - 500;
  const uint32_t after = 700;
  TEST_ASSERT_TRUE(isLater(after, before));
  TEST_ASSERT_FALSE(isLater(before, after));
  TEST_ASSERT_FALSE(isLater(after, after));
  TEST_ASSERT_TRUE(isLater(before + 1, before));
}

void test_ms_since_counts_across_the_wrap() {
  const uint32_t stamp = UINT32_MAX - 499;
  TEST_ASSERT_EQUAL_UINT32(0, msSince(stamp, stamp));
  TEST_ASSERT_EQUAL_UINT32(499, msSince(UINT32_MAX, stamp));
  TEST_ASSERT_EQUAL_UINT32(500, msSince(0, stamp));
  TEST_ASSERT_EQUAL_UINT32(1700, msSince(1200, stamp));
}

// A stamp written by another task just after nowMs was read sits ahead of it;
// it reads as fresh, including when the two straddle the wrap.
void test_ms_since_reads_a_stamp_ahead_as_fresh() {
  TEST_ASSERT_EQUAL_UINT32(0, msSince(UINT32_MAX - 2, 3));
  TEST_ASSERT_EQUAL_UINT32(0, msSince(5000, 5000 + kStampSkewMs));
  TEST_ASSERT_EQUAL_UINT32(0, msSince(5000, 5000 + 1));
  // Past the skew, the stamp reads as old: nearly a whole wrap.
  TEST_ASSERT_TRUE(msSince(5000, 5000 + kStampSkewMs + 2) > UINT32_MAX / 2);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_time_reached_waits_for_a_deadline_past_the_wrap);
  RUN_TEST(test_time_reached_holds_a_deadline_before_the_wrap_after_it);
  RUN_TEST(test_is_later_orders_stamps_across_the_wrap);
  RUN_TEST(test_ms_since_counts_across_the_wrap);
  RUN_TEST(test_ms_since_reads_a_stamp_ahead_as_fresh);
  return UNITY_END();
}
