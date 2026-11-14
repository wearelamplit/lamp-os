// Pins the post-update self-check for when rollback is on: it arms only on an
// unverified image, asks once after the settle window with BLE up, waits while
// BLE is down, stays pending until a mark succeeds, and holds firmware offers
// back while pending.

#include <unity.h>

#include <cstdint>

#include "../../src/components/firmware/ota_mark_valid.cpp"

using lamp::MarkValidCheck;
using lamp::offerWaitsForMarkValid;

void setUp(void) {}
void tearDown(void) {}

constexpr uint32_t kSetupEndMs = 4200;

void test_a_verified_image_never_asks() {
  MarkValidCheck c;
  c.arm(false, kSetupEndMs);
  TEST_ASSERT_FALSE(c.pending());
  for (uint32_t t = kSetupEndMs; t < kSetupEndMs + 120000; t += 1000) {
    TEST_ASSERT_FALSE(c.due(t, true));
  }
}

void test_an_unverified_image_asks_once_after_the_settle_window() {
  MarkValidCheck c;
  c.arm(true, kSetupEndMs);
  TEST_ASSERT_TRUE(c.pending());
  TEST_ASSERT_FALSE(c.due(kSetupEndMs + MarkValidCheck::kSettleMs - 1, true));
  TEST_ASSERT_TRUE(c.due(kSetupEndMs + MarkValidCheck::kSettleMs, true));
  TEST_ASSERT_FALSE(c.due(kSetupEndMs + MarkValidCheck::kSettleMs + 1, true));
  TEST_ASSERT_FALSE(c.due(kSetupEndMs + 600000, true));
  TEST_ASSERT_TRUE(c.pending());
  c.validated();
  TEST_ASSERT_FALSE(c.pending());
}

void test_without_ble_it_waits_and_asks_again() {
  MarkValidCheck c;
  c.arm(true, kSetupEndMs);
  const uint32_t settled = kSetupEndMs + MarkValidCheck::kSettleMs;
  TEST_ASSERT_FALSE(c.due(settled, false));
  TEST_ASSERT_FALSE(c.due(settled + MarkValidCheck::kRetryMs - 1, true));
  TEST_ASSERT_TRUE(c.due(settled + MarkValidCheck::kRetryMs, true));
}

void test_a_failed_mark_leaves_the_image_pending() {
  MarkValidCheck c;
  c.arm(true, kSetupEndMs);
  TEST_ASSERT_TRUE(c.due(kSetupEndMs + MarkValidCheck::kSettleMs, true));
  TEST_ASSERT_TRUE(c.pending());
  TEST_ASSERT_FALSE(c.due(kSetupEndMs + MarkValidCheck::kSettleMs + 10000, true));
}

void test_the_settle_window_spans_the_millis_wrap() {
  MarkValidCheck c;
  const uint32_t start = UINT32_MAX - 1000;
  c.arm(true, start);
  // Still before the wrap, with the deadline already past it.
  TEST_ASSERT_FALSE(c.due(start + 500, true));
  TEST_ASSERT_FALSE(c.due(start + 2000, true));
  TEST_ASSERT_TRUE(c.due(start + MarkValidCheck::kSettleMs, true));
}

void test_only_a_firmware_offer_waits_for_the_mark() {
  TEST_ASSERT_TRUE(offerWaitsForMarkValid(false, true));
  TEST_ASSERT_FALSE(offerWaitsForMarkValid(true, true));
  TEST_ASSERT_FALSE(offerWaitsForMarkValid(false, false));
  TEST_ASSERT_FALSE(offerWaitsForMarkValid(true, false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_verified_image_never_asks);
  RUN_TEST(test_an_unverified_image_asks_once_after_the_settle_window);
  RUN_TEST(test_without_ble_it_waits_and_asks_again);
  RUN_TEST(test_a_failed_mark_leaves_the_image_pending);
  RUN_TEST(test_the_settle_window_spans_the_millis_wrap);
  RUN_TEST(test_only_a_firmware_offer_waits_for_the_mark);
  return UNITY_END();
}
