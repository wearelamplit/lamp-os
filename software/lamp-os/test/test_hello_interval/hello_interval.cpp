// Pins the boot-burst HELLO cadence: fast (5 s) for the first 30 s of uptime,
// then the steady 30 s budget.

#include <unity.h>
#include <cstdint>

#include "components/network/mesh/hello_interval.hpp"  // -I src on native

using lamp::helloIntervalMs;

void setUp(void) {}
void tearDown(void) {}

void test_burst_window_uses_fast_interval() {
  TEST_ASSERT_EQUAL_UINT32(LAMP_HELLO_BURST_INTERVAL_MS, helloIntervalMs(0));
  TEST_ASSERT_EQUAL_UINT32(LAMP_HELLO_BURST_INTERVAL_MS, helloIntervalMs(5000));
  TEST_ASSERT_EQUAL_UINT32(LAMP_HELLO_BURST_INTERVAL_MS, helloIntervalMs(29999));
}

void test_after_window_uses_steady_interval() {
  TEST_ASSERT_EQUAL_UINT32(LAMP_HELLO_INTERVAL_MS, helloIntervalMs(30000));
  TEST_ASSERT_EQUAL_UINT32(LAMP_HELLO_INTERVAL_MS, helloIntervalMs(60000));
}

// The burst belongs to the first 30 s after boot only, not to the first 30 s
// after each 49.7-day millis() wrap.
void test_burst_does_not_return_past_the_millis_wrap() {
  const uint64_t wrapped = uint64_t{1} << 32;
  TEST_ASSERT_EQUAL_UINT32(LAMP_HELLO_INTERVAL_MS, helloIntervalMs(wrapped));
  TEST_ASSERT_EQUAL_UINT32(LAMP_HELLO_INTERVAL_MS, helloIntervalMs(wrapped + 5000));
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_burst_window_uses_fast_interval);
  RUN_TEST(test_after_window_uses_steady_interval);
  RUN_TEST(test_burst_does_not_return_past_the_millis_wrap);
  return UNITY_END();
}
