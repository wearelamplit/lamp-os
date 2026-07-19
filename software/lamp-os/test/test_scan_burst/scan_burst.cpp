// Pins the on-demand scan-burst gate: fires only when armed, idle, connected,
// and past the rate-limit window.

#include <unity.h>
#include <cstdint>

#include "components/network/ble/scan_burst.hpp"  // -I src on native

using ble_control::scanBurstReady;

static constexpr uint32_t kInterval = 5000;

void setUp(void) {}
void tearDown(void) {}

void test_first_burst_fires_when_armed() {
  // lastBurstMs 0 = no prior burst, allowed regardless of `now`.
  TEST_ASSERT_TRUE(scanBurstReady(true, false, true, 3000, 0, kInterval));
}

void test_blocked_when_not_requested() {
  TEST_ASSERT_FALSE(scanBurstReady(false, false, true, 100000, 0, kInterval));
}

void test_blocked_when_active() {
  TEST_ASSERT_FALSE(scanBurstReady(true, true, true, 100000, 0, kInterval));
}

void test_blocked_when_disconnected() {
  TEST_ASSERT_FALSE(scanBurstReady(true, false, false, 100000, 0, kInterval));
}

void test_rate_limited_within_window() {
  TEST_ASSERT_FALSE(scanBurstReady(true, false, true, 14000, 10000, kInterval));
}

void test_fires_after_window() {
  TEST_ASSERT_TRUE(scanBurstReady(true, false, true, 15000, 10000, kInterval));
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_first_burst_fires_when_armed);
  RUN_TEST(test_blocked_when_not_requested);
  RUN_TEST(test_blocked_when_active);
  RUN_TEST(test_blocked_when_disconnected);
  RUN_TEST(test_rate_limited_within_window);
  RUN_TEST(test_fires_after_window);
  return UNITY_END();
}
