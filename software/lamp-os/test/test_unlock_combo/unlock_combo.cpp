// Native tests for the staff touch-unlock state machine. Injected clock.

#include <unity.h>

#include "lamps/staff/unlock_combo.hpp"
#include "../../src/lamps/staff/unlock_combo.cpp"

using staff::UnlockCombo;
using staff::UnlockTiming;

void setUp(void) {}
void tearDown(void) {}

void test_starts_locked() {
  UnlockCombo c;
  TEST_ASSERT_FALSE(c.unlocked());
}

void test_correct_sequence_unlocks() {
  UnlockCombo c;  // default stepWindowMs 1500
  c.stokeLongReleased(1000);
  TEST_ASSERT_TRUE(c.armed());
  c.topPadTapped(1800);  // within 1500 of 1000
  TEST_ASSERT_TRUE(c.unlocked());
}

void test_tap_after_window_relocks() {
  UnlockCombo c;
  c.stokeLongReleased(1000);
  c.topPadTapped(3000);  // 2000ms later, past 1500 window
  TEST_ASSERT_FALSE(c.unlocked());
  // and it is fully re-locked, not left armed
  TEST_ASSERT_FALSE(c.armed());
}

void test_tick_times_out_arm() {
  UnlockCombo c;
  c.stokeLongReleased(1000);
  TEST_ASSERT_TRUE(c.armed());
  c.tick(3000);  // 2000ms later, past the 1500ms window
  TEST_ASSERT_FALSE(c.armed());
  // a late tap now does nothing (must re-arm first)
  c.topPadTapped(3100);
  TEST_ASSERT_FALSE(c.unlocked());
}

void test_tap_without_arm_does_nothing() {
  UnlockCombo c;
  c.topPadTapped(500);  // no stoke step first
  TEST_ASSERT_FALSE(c.unlocked());
}

void test_stays_unlocked() {
  UnlockCombo c;
  c.stokeLongReleased(1000);
  c.topPadTapped(1200);
  TEST_ASSERT_TRUE(c.unlocked());
  // subsequent gestures / timeouts don't re-lock
  c.tick(100000);
  c.stokeLongReleased(100000);
  TEST_ASSERT_TRUE(c.unlocked());
}

void test_re_arm_restarts_window() {
  UnlockCombo c;
  c.stokeLongReleased(1000);
  c.stokeLongReleased(5000);  // re-arm resets the clock
  c.topPadTapped(6000);       // within 1500 of 5000
  TEST_ASSERT_TRUE(c.unlocked());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_locked);
  RUN_TEST(test_correct_sequence_unlocks);
  RUN_TEST(test_tap_after_window_relocks);
  RUN_TEST(test_tick_times_out_arm);
  RUN_TEST(test_tap_without_arm_does_nothing);
  RUN_TEST(test_stays_unlocked);
  RUN_TEST(test_re_arm_restarts_window);
  return UNITY_END();
}
