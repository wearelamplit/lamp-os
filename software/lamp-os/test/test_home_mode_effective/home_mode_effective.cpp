// Native truth-table tests for effectiveHomeModeFromConfig.
// Includes only home_mode_logic.hpp — no BLE/WiFi/Arduino deps.

#include <unity.h>

#include "core/home_mode_logic.hpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

void test_disabled_always_false() {
  TEST_ASSERT_FALSE(effectiveHomeModeFromConfig(false, false, true, false));
  TEST_ASSERT_FALSE(effectiveHomeModeFromConfig(false, true, false, true));
}

void test_manual_mode_always_true_when_enabled() {
  // networkBound=false: home mode is on regardless of SSID state.
  TEST_ASSERT_TRUE(effectiveHomeModeFromConfig(true, false, true, false));
  TEST_ASSERT_TRUE(effectiveHomeModeFromConfig(true, false, false, false));
  TEST_ASSERT_TRUE(effectiveHomeModeFromConfig(true, false, false, true));
}

void test_network_bound_requires_ssid_and_visible() {
  TEST_ASSERT_FALSE(effectiveHomeModeFromConfig(true, true, true, false));   // no ssid
  TEST_ASSERT_FALSE(effectiveHomeModeFromConfig(true, true, false, false));  // ssid set, not visible
  TEST_ASSERT_TRUE(effectiveHomeModeFromConfig(true, true, false, true));    // ssid + visible
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_always_false);
  RUN_TEST(test_manual_mode_always_true_when_enabled);
  RUN_TEST(test_network_bound_requires_ssid_and_visible);
  return UNITY_END();
}
