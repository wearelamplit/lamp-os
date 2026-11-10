// Native tests for lampos/clock.hpp, the wrap-safe millis() comparisons shared
// by lamp and wisp. Every case straddles the 32-bit wrap at 2^32 ms.

#include <unity.h>

#include <cstdint>

#include <lampos/clock.hpp>

using lampos::frameDeltaMs;
using lampos::isLater;
using lampos::kMaxFrameDeltaMs;
using lampos::kStampLeadMs;
using lampos::kRetiredAgeMs;
using lampos::msSince;
using lampos::retireStamp;
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
  TEST_ASSERT_EQUAL_UINT32(0, msSince(5000, 5000 + kStampLeadMs));
  TEST_ASSERT_EQUAL_UINT32(0, msSince(5000, 5000 + 1));
  // Past the lead, the stamp reads as old: nearly a whole wrap.
  TEST_ASSERT_TRUE(msSince(5000, 5000 + kStampLeadMs + 1) > UINT32_MAX / 2);
}

void test_retire_stamp_leaves_a_stamp_younger_than_half_a_wrap() {
  const uint32_t stamp = UINT32_MAX - 99;
  TEST_ASSERT_EQUAL_UINT32(stamp, retireStamp(stamp + 400, stamp));
  TEST_ASSERT_EQUAL_UINT32(stamp, retireStamp(stamp + kRetiredAgeMs, stamp));
  TEST_ASSERT_EQUAL_UINT32(stamp, retireStamp(stamp - kStampLeadMs, stamp));
}

void test_retire_stamp_holds_an_older_stamp_at_half_a_wrap() {
  const uint32_t stamp = UINT32_MAX - 99;
  const uint32_t now = stamp + kRetiredAgeMs + 9000;
  TEST_ASSERT_EQUAL_UINT32(kRetiredAgeMs, msSince(now, retireStamp(now, stamp)));
  // The held stamp would land on 0, which a caller may read as never set.
  const uint32_t nowAtHalf = kRetiredAgeMs;
  TEST_ASSERT_NOT_EQUAL(0, retireStamp(nowAtHalf, UINT32_MAX - 10));
  TEST_ASSERT_TRUE(msSince(nowAtHalf, retireStamp(nowAtHalf, UINT32_MAX - 10)) > kRetiredAgeMs / 2);
}

// An owner retiring a stamp every few days keeps it aged out through the
// wrap, where the raw stamp would read 300 ms old.
void test_retired_stamp_stays_aged_out_a_wrap_later() {
  const uint32_t stamp = 1000;
  uint32_t held = stamp;
  for (uint64_t t = stamp; t < stamp + (1ull << 32); t += 1ull << 28) {
    held = retireStamp(static_cast<uint32_t>(t), held);
  }
  const uint32_t aWrapLater = stamp + 300;
  TEST_ASSERT_EQUAL_UINT32(300, msSince(aWrapLater, stamp));
  TEST_ASSERT_TRUE(msSince(aWrapLater, held) > kRetiredAgeMs / 2);
}

void test_frame_delta_passes_a_normal_frame_across_the_wrap() {
  TEST_ASSERT_EQUAL_UINT32(16, frameDeltaMs(10, UINT32_MAX - 5));
  TEST_ASSERT_EQUAL_UINT32(kMaxFrameDeltaMs - 1,
                           frameDeltaMs(kMaxFrameDeltaMs - 1, 0));
  TEST_ASSERT_EQUAL_UINT32(kMaxFrameDeltaMs, frameDeltaMs(kMaxFrameDeltaMs, 0));
}

// Frozen for three weeks, then for long enough that the wrap leaves the raw
// delta at ten days: either way the first frame steps one capped frame.
void test_frame_delta_caps_the_first_frame_after_weeks_frozen() {
  constexpr uint64_t kDayMs = 24ull * 60 * 60 * 1000;
  const uint32_t last = UINT32_MAX - 1000;
  TEST_ASSERT_EQUAL_UINT32(kMaxFrameDeltaMs,
                           frameDeltaMs(static_cast<uint32_t>(last + 21 * kDayMs), last));
  const uint64_t frozen = (1ull << 32) + 10 * kDayMs;
  TEST_ASSERT_EQUAL_UINT32(kMaxFrameDeltaMs,
                           frameDeltaMs(static_cast<uint32_t>(last + frozen), last));
  TEST_ASSERT_EQUAL_UINT32(kMaxFrameDeltaMs, frameDeltaMs(last + kMaxFrameDeltaMs + 1, last));
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
  RUN_TEST(test_retire_stamp_leaves_a_stamp_younger_than_half_a_wrap);
  RUN_TEST(test_retire_stamp_holds_an_older_stamp_at_half_a_wrap);
  RUN_TEST(test_retired_stamp_stays_aged_out_a_wrap_later);
  RUN_TEST(test_frame_delta_passes_a_normal_frame_across_the_wrap);
  RUN_TEST(test_frame_delta_caps_the_first_frame_after_weeks_frozen);
  return UNITY_END();
}
