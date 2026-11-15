// Pins the post-update self-check: it arms only on an unverified image, asks
// for the mark after the settle window once BLE has run, asks again while the
// mark fails, waits while BLE has not run, and rolls back an image whose BLE
// never comes up.

#include <unity.h>

#include <cstdint>

#include "../../src/components/firmware/ota_mark_valid.cpp"

using lamp::MarkValidCheck;
using Step = lamp::MarkValidCheck::Step;

void setUp(void) {}
void tearDown(void) {}

constexpr uint32_t kSetupEndMs = 4200;
constexpr uint32_t kSettled = kSetupEndMs + MarkValidCheck::kSettleMs;

void test_a_verified_image_never_asks() {
  MarkValidCheck c;
  c.arm(false, kSetupEndMs, false);
  TEST_ASSERT_FALSE(c.pending());
  for (uint32_t t = kSetupEndMs; t < kSettled + 2 * MarkValidCheck::kBleWaitMs; t += 1000) {
    TEST_ASSERT_TRUE(c.poll(t, true) == Step::Wait);
    TEST_ASSERT_TRUE(c.poll(t, false) == Step::Wait);
  }
}

void test_an_unverified_image_is_marked_after_the_settle_window() {
  MarkValidCheck c;
  c.arm(true, kSetupEndMs, false);
  TEST_ASSERT_TRUE(c.pending());
  TEST_ASSERT_TRUE(c.poll(kSettled - 1, true) == Step::Wait);
  TEST_ASSERT_TRUE(c.poll(kSettled, true) == Step::MarkValid);
  TEST_ASSERT_TRUE(c.poll(kSettled + 1, true) == Step::Wait);
  TEST_ASSERT_TRUE(c.pending());
  c.validated();
  TEST_ASSERT_FALSE(c.pending());
  for (uint32_t t = kSettled; t < kSettled + 2 * MarkValidCheck::kBleWaitMs; t += 1000) {
    TEST_ASSERT_TRUE(c.poll(t, false) == Step::Wait);
  }
}

void test_a_failed_mark_is_asked_again() {
  MarkValidCheck c;
  c.arm(true, kSetupEndMs, false);
  TEST_ASSERT_TRUE(c.poll(kSettled, true) == Step::MarkValid);
  TEST_ASSERT_TRUE(c.poll(kSettled + MarkValidCheck::kRetryMs - 1, true) == Step::Wait);
  TEST_ASSERT_TRUE(c.poll(kSettled + MarkValidCheck::kRetryMs, true) == Step::MarkValid);
  TEST_ASSERT_TRUE(c.pending());
}

void test_without_ble_it_waits_then_marks_once_ble_is_up() {
  MarkValidCheck c;
  c.arm(true, kSetupEndMs, false);
  TEST_ASSERT_TRUE(c.poll(kSettled, false) == Step::Wait);
  TEST_ASSERT_TRUE(c.poll(kSettled + MarkValidCheck::kRetryMs - 1, true) == Step::Wait);
  TEST_ASSERT_TRUE(c.poll(kSettled + MarkValidCheck::kRetryMs, true) == Step::MarkValid);
}

void test_an_image_whose_ble_never_starts_is_rolled_back() {
  MarkValidCheck c;
  c.arm(true, kSetupEndMs, false);
  uint32_t t = kSettled;
  for (; t < kSettled + MarkValidCheck::kBleWaitMs; t += 700) {
    TEST_ASSERT_TRUE(c.poll(t, false) == Step::Wait);
  }
  // The first poll due at or past the BLE wait rolls back, and a rollback that
  // returns is asked again.
  Step step = Step::Wait;
  uint32_t rolledAt = 0;
  const uint32_t giveUp = kSettled + MarkValidCheck::kBleWaitMs + 2 * MarkValidCheck::kRetryMs;
  for (; step == Step::Wait && t < giveUp; t += 700) {
    step = c.poll(t, false);
    rolledAt = t;
  }
  TEST_ASSERT_TRUE(step == Step::RollBack);
  TEST_ASSERT_TRUE(rolledAt - kSettled < MarkValidCheck::kBleWaitMs + MarkValidCheck::kRetryMs + 700);
  TEST_ASSERT_TRUE(c.poll(rolledAt + MarkValidCheck::kRetryMs, false) == Step::RollBack);
  TEST_ASSERT_TRUE(c.pending());
}

void test_ble_up_at_the_last_moment_marks_instead_of_rolling_back() {
  MarkValidCheck c;
  c.arm(true, kSetupEndMs, false);
  TEST_ASSERT_TRUE(c.poll(kSettled + MarkValidCheck::kBleWaitMs + 3000, true) == Step::MarkValid);
}

void test_ble_that_ran_then_stopped_for_the_web_app_still_marks() {
  MarkValidCheck c;
  c.arm(true, kSetupEndMs, false);
  TEST_ASSERT_TRUE(c.poll(kSetupEndMs + 7000, true) == Step::Wait);
  TEST_ASSERT_TRUE(c.poll(kSettled, false) == Step::MarkValid);

  MarkValidCheck upAtArm;
  upAtArm.arm(true, kSetupEndMs, true);
  TEST_ASSERT_TRUE(upAtArm.poll(kSettled - 1, false) == Step::Wait);
  TEST_ASSERT_TRUE(upAtArm.poll(kSettled, false) == Step::MarkValid);
  for (uint32_t t = kSettled + MarkValidCheck::kRetryMs; t < kSettled + 2 * MarkValidCheck::kBleWaitMs;
       t += MarkValidCheck::kRetryMs) {
    TEST_ASSERT_TRUE(upAtArm.poll(t, false) == Step::MarkValid);
  }
}

void test_the_settle_window_spans_the_millis_wrap() {
  MarkValidCheck c;
  const uint32_t start = UINT32_MAX - 1000;
  c.arm(true, start, false);
  // Still before the wrap, with the deadline already past it.
  TEST_ASSERT_TRUE(c.poll(start + 500, true) == Step::Wait);
  TEST_ASSERT_TRUE(c.poll(start + 2000, true) == Step::Wait);
  TEST_ASSERT_TRUE(c.poll(start + MarkValidCheck::kSettleMs - 1, true) == Step::Wait);
  TEST_ASSERT_TRUE(c.poll(start + MarkValidCheck::kSettleMs, true) == Step::MarkValid);
}

void test_the_ble_wait_spans_the_millis_wrap() {
  MarkValidCheck c;
  const uint32_t start = UINT32_MAX - MarkValidCheck::kSettleMs - 1000;
  c.arm(true, start, false);
  const uint32_t settled = start + MarkValidCheck::kSettleMs;
  TEST_ASSERT_TRUE(c.poll(settled, false) == Step::Wait);
  TEST_ASSERT_TRUE(c.poll(settled + MarkValidCheck::kBleWaitMs - MarkValidCheck::kRetryMs, false) ==
                   Step::Wait);
  TEST_ASSERT_TRUE(c.poll(settled + MarkValidCheck::kBleWaitMs, false) == Step::RollBack);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_verified_image_never_asks);
  RUN_TEST(test_an_unverified_image_is_marked_after_the_settle_window);
  RUN_TEST(test_a_failed_mark_is_asked_again);
  RUN_TEST(test_without_ble_it_waits_then_marks_once_ble_is_up);
  RUN_TEST(test_an_image_whose_ble_never_starts_is_rolled_back);
  RUN_TEST(test_ble_up_at_the_last_moment_marks_instead_of_rolling_back);
  RUN_TEST(test_ble_that_ran_then_stopped_for_the_web_app_still_marks);
  RUN_TEST(test_the_settle_window_spans_the_millis_wrap);
  RUN_TEST(test_the_ble_wait_spans_the_millis_wrap);
  return UNITY_END();
}
