#include <unity.h>

#include "../../src/util/levels.hpp"

void setUp() {}
void tearDown() {}

void test_effective_ceiling() {
  TEST_ASSERT_EQUAL_UINT8(170, lamp::effectiveCeiling(230, 170));  // Standard under variant 230
  TEST_ASSERT_EQUAL_UINT8(230, lamp::effectiveCeiling(230, 230));  // Bright ties variant
  TEST_ASSERT_EQUAL_UINT8(120, lamp::effectiveCeiling(230, 120));  // Saver
  TEST_ASSERT_EQUAL_UINT8(200, lamp::effectiveCeiling(200, 230));  // variant wins when lower
  TEST_ASSERT_EQUAL_UINT8(1, lamp::effectiveCeiling(230, 0));      // clamp floor 1
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_effective_ceiling);
  return UNITY_END();
}
