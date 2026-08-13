// Native tests for the STATE edge-burst timing predicate in PresenceBeacon.
//
// stateBurstDue gates the extra STATE copies emitted after a paint-mode edge:
//   - no window (burstUntilMs == 0) is never due,
//   - inside the window a copy is due only after intervalMs since the last emit,
//   - a closed window (now past burstUntilMs) is never due.

#include <unity.h>

#include <cstdint>

#include "status/presence_beacon.hpp"

namespace {

constexpr uint32_t kInterval = 700;

void test_no_window_never_due(void) {
  TEST_ASSERT_FALSE(wisp::stateBurstDue(5000, 0, 4000, kInterval));
}

void test_open_window_after_interval_is_due(void) {
  TEST_ASSERT_TRUE(wisp::stateBurstDue(2000, 3000, 1300, kInterval));
}

void test_open_window_too_soon_not_due(void) {
  TEST_ASSERT_FALSE(wisp::stateBurstDue(2000, 3000, 1500, kInterval));
}

void test_interval_boundary_is_due(void) {
  TEST_ASSERT_TRUE(wisp::stateBurstDue(2000, 3000, 1300, kInterval));
  TEST_ASSERT_FALSE(wisp::stateBurstDue(1999, 3000, 1300, kInterval));
}

void test_closed_window_not_due(void) {
  TEST_ASSERT_FALSE(wisp::stateBurstDue(3000, 3000, 100, kInterval));
  TEST_ASSERT_FALSE(wisp::stateBurstDue(3500, 3000, 100, kInterval));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_window_never_due);
  RUN_TEST(test_open_window_after_interval_is_due);
  RUN_TEST(test_open_window_too_soon_not_due);
  RUN_TEST(test_interval_boundary_is_due);
  RUN_TEST(test_closed_window_not_due);
  return UNITY_END();
}
