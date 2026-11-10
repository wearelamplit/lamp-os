// Native-host tests for BootRecovery: the BOOT-hold that clears a forgotten
// control password, isolated from Arduino so the timing is testable.

#include <unity.h>

#include <cstdint>

#include "../../src/core/boot_recovery.hpp"

using lamp::BootRecovery;
using lamp::kBootRecoveryHoldMs;
using lamp::kBootRecoveryWindowMs;

void setUp(void) {}
void tearDown(void) {}

void test_full_hold_fires_once() {
  BootRecovery r;
  TEST_ASSERT_FALSE(r.update(true, 1000));
  TEST_ASSERT_FALSE(r.update(true, 1000 + kBootRecoveryHoldMs - 1));
  TEST_ASSERT_TRUE(r.update(true, 1000 + kBootRecoveryHoldMs));
  // Fires exactly once; a still-held button must not re-trigger.
  TEST_ASSERT_FALSE(r.update(true, 1000 + kBootRecoveryHoldMs + 1));
}

void test_release_resets_the_accumulator() {
  BootRecovery r;
  r.update(true, 100);
  r.update(true, 100 + kBootRecoveryHoldMs - 1);
  TEST_ASSERT_FALSE(r.update(false, 100 + kBootRecoveryHoldMs));
  // Re-press starts over, so the earlier partial hold does not count.
  TEST_ASSERT_FALSE(r.update(true, 100 + kBootRecoveryHoldMs + 1));
  TEST_ASSERT_FALSE(r.update(true, 100 + 2 * kBootRecoveryHoldMs - 1));
  TEST_ASSERT_TRUE(r.update(true, 100 + 2 * kBootRecoveryHoldMs + 1));
}

// A hold begun inside the window may complete past it: the gate is when the
// press starts, so a 5s hold begun at 59s still recovers the lamp.
void test_hold_begun_inside_window_completes_past_it() {
  BootRecovery r;
  const uint32_t start = kBootRecoveryWindowMs - 1000;
  TEST_ASSERT_FALSE(r.update(true, start));
  TEST_ASSERT_TRUE(r.update(true, start + kBootRecoveryHoldMs));
}

// Past the first minute a stray GPIO0 pull must not clear the password.
void test_hold_started_after_the_window_never_fires() {
  BootRecovery r;
  const uint32_t late = kBootRecoveryWindowMs + 1;
  TEST_ASSERT_FALSE(r.update(true, late));
  TEST_ASSERT_FALSE(r.update(true, late + kBootRecoveryHoldMs));
  TEST_ASSERT_FALSE(r.update(true, late + 10 * kBootRecoveryHoldMs));
}

// millis() restarts from 0 every 49.7 days; the window is the first minute
// after boot, so it must not reopen once uptime passes 2^32 ms.
void test_window_stays_closed_past_the_millis_wrap() {
  BootRecovery r;
  const uint64_t wrapped = (uint64_t{1} << 32) + 1000;
  TEST_ASSERT_FALSE(r.update(true, wrapped));
  TEST_ASSERT_FALSE(r.update(true, wrapped + kBootRecoveryHoldMs));
  TEST_ASSERT_FALSE(r.update(true, wrapped + 10 * kBootRecoveryHoldMs));
}

void test_brief_presses_never_fire() {
  BootRecovery r;
  for (uint32_t t = 0; t < kBootRecoveryWindowMs; t += 1000) {
    TEST_ASSERT_FALSE(r.update(true, t));
    TEST_ASSERT_FALSE(r.update(false, t + 500));
  }
}

void test_button_never_pressed_never_fires() {
  BootRecovery r;
  for (uint32_t t = 0; t < kBootRecoveryWindowMs; t += 100) {
    TEST_ASSERT_FALSE(r.update(false, t));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_full_hold_fires_once);
  RUN_TEST(test_release_resets_the_accumulator);
  RUN_TEST(test_hold_begun_inside_window_completes_past_it);
  RUN_TEST(test_hold_started_after_the_window_never_fires);
  RUN_TEST(test_window_stays_closed_past_the_millis_wrap);
  RUN_TEST(test_brief_presses_never_fire);
  RUN_TEST(test_button_never_pressed_never_fires);
  return UNITY_END();
}
