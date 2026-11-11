#include <unity.h>
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

void test_unicast_gate_closed_before_min_gap() {
  TEST_ASSERT_FALSE(wisp::unicastGateOpen(1010, 1000, 18));
}
void test_unicast_gate_open_at_min_gap() {
  TEST_ASSERT_TRUE(wisp::unicastGateOpen(1018, 1000, 18));
}
void test_unicast_gate_open_past_min_gap() {
  TEST_ASSERT_TRUE(wisp::unicastGateOpen(5000, 1000, 18));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_slot_divides_interval_by_count);
  RUN_TEST(test_slot_zero_when_no_lamps);
  RUN_TEST(test_rotation_wraps);
  RUN_TEST(test_unicast_gate_closed_before_min_gap);
  RUN_TEST(test_unicast_gate_open_at_min_gap);
  RUN_TEST(test_unicast_gate_open_past_min_gap);
  return UNITY_END();
}
