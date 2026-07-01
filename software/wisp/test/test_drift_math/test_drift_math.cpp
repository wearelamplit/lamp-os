#include <unity.h>
#include <initializer_list>
#include "paint/drift.hpp"

void setUp() {}
void tearDown() {}

void test_slot_divides_interval_by_count() {
  TEST_ASSERT_EQUAL_UINT32(30000, wisp::driftSlotMs(120000, 4));
  TEST_ASSERT_EQUAL_UINT32(3750,  wisp::driftSlotMs(30000, 8));
  TEST_ASSERT_EQUAL_UINT32(120000, wisp::driftSlotMs(120000, 1));
}
void test_slot_zero_when_no_lamps() {
  TEST_ASSERT_EQUAL_UINT32(0, wisp::driftSlotMs(120000, 0));
}
void test_rotation_wraps() {
  TEST_ASSERT_EQUAL_UINT32(1, wisp::nextDriftIdx(0, 3));
  TEST_ASSERT_EQUAL_UINT32(0, wisp::nextDriftIdx(2, 3));   // wrap
  TEST_ASSERT_EQUAL_UINT32(0, wisp::nextDriftIdx(5, 0));   // n==0 guard
}
void test_drift_fade_with_zero_pct() {
  for (uint32_t rnd : {0u, 1u, 0xFFFFFFFFu}) {
    TEST_ASSERT_EQUAL_UINT32(20000, wisp::driftFadeMs(120000, 0, rnd));
    TEST_ASSERT_EQUAL_UINT32(20000, wisp::driftFadeMs(3600000, 0, rnd));
  }
}
void test_drift_fade_with_max_pct_min_rnd() {
  TEST_ASSERT_EQUAL_UINT32(20000, wisp::driftFadeMs(120000, 100, 0));
  TEST_ASSERT_EQUAL_UINT32(20000, wisp::driftFadeMs(3600000, 100, 0));
}
void test_drift_fade_with_max_pct_reaches_interval() {
  // When fadePct=100, fadeMax = intervalMs, so max achievable is intervalMs.
  // Use rnd=(intervalMs-20000) to reach the maximum.
  TEST_ASSERT_EQUAL_UINT32(120000, wisp::driftFadeMs(120000, 100, 100000));
  TEST_ASSERT_EQUAL_UINT32(3600000, wisp::driftFadeMs(3600000, 100, 3580000));
}
void test_drift_fade_bounds() {
  struct {
    uint32_t intervalMs;
    uint8_t fadePct;
    uint32_t rnd;
  } cases[] = {
    {120000, 50, 0},
    {120000, 50, 12345u},
    {120000, 50, 0xFFFFFFFFu},
    {3600000, 25, 5000u},
    {3600000, 75, 999999u},
    {30000, 100, 11111u},
    {20001, 50, 8888u},
  };
  for (auto& c : cases) {
    uint32_t f = wisp::driftFadeMs(c.intervalMs, c.fadePct, c.rnd);
    uint32_t fadeMax = c.intervalMs > 20000
        ? 20000 + static_cast<uint32_t>((uint64_t)(c.intervalMs - 20000) * c.fadePct / 100)
        : 20000;
    TEST_ASSERT_TRUE(f >= 20000);
    TEST_ASSERT_TRUE(f <= fadeMax);
    TEST_ASSERT_TRUE(f <= c.intervalMs);
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_slot_divides_interval_by_count);
  RUN_TEST(test_slot_zero_when_no_lamps);
  RUN_TEST(test_rotation_wraps);
  RUN_TEST(test_drift_fade_with_zero_pct);
  RUN_TEST(test_drift_fade_with_max_pct_min_rnd);
  RUN_TEST(test_drift_fade_with_max_pct_reaches_interval);
  RUN_TEST(test_drift_fade_bounds);
  return UNITY_END();
}
