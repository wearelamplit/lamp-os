// Native tests for lamp::uptimeSince over the mock 64-bit uptime.

#include <unity.h>

#include "util/uptime.hpp"

void setUp(void) {
  g_mock_millis_wraps = 0;
  set_mock_millis(0);
}
void tearDown(void) {}

void test_since_counts_across_the_millis_wrap() {
  g_mock_millis_wraps = 1;
  set_mock_millis(500);
  TEST_ASSERT_EQUAL_UINT32(1000, lamp::uptimeSince((1ull << 32) - 500));
}

void test_since_saturates_past_32_bits() {
  g_mock_millis_wraps = 3;
  set_mock_millis(10);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, lamp::uptimeSince(5));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_since_counts_across_the_millis_wrap);
  RUN_TEST(test_since_saturates_past_32_bits);
  return UNITY_END();
}
