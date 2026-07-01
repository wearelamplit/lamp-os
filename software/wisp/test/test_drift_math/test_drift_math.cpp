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
void test_fade_roll_in_bounds() {
  TEST_ASSERT_EQUAL_UINT16(6000, wisp::rollDriftFadeMs(0));
  for (uint32_t r : {1u, 8501u, 17000u, 123456789u, 0xFFFFFFFFu}) {
    uint16_t f = wisp::rollDriftFadeMs(r);
    TEST_ASSERT_TRUE(f >= 6000 && f <= 23000);
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_slot_divides_interval_by_count);
  RUN_TEST(test_slot_zero_when_no_lamps);
  RUN_TEST(test_rotation_wraps);
  RUN_TEST(test_fade_roll_in_bounds);
  return UNITY_END();
}
