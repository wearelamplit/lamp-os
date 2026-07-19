// Native-host unit tests for otaAcceptable.
//
// Exercises the full acceptance matrix from the channel-promotion plan.
// Links the production ota_channel.cpp directly (same pattern as
// test_firmware_signature).

#include <unity.h>

#include "components/firmware/ota_channel.hpp"
#include "components/firmware/ota_channel.cpp"

void setUp(void) {}
void tearDown(void) {}

// --- Intra-channel accept ---------------------------------------------------

void test_intra_beta_upgrade_accepted(void) {
  TEST_ASSERT_TRUE(otaAcceptable("standard-beta", 100, "standard-beta", 101));
}

void test_intra_stable_upgrade_accepted(void) {
  TEST_ASSERT_TRUE(otaAcceptable("standard-stable", 100, "standard-stable", 101));
}

// --- Intra-channel reject ---------------------------------------------------

void test_intra_beta_equal_version_rejected(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-beta", 100, "standard-beta", 100));
}

void test_intra_stable_equal_version_rejected(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-stable", 100, "standard-stable", 100));
}

void test_intra_beta_downgrade_rejected(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-beta", 101, "standard-beta", 100));
}

// --- Promotion accept -------------------------------------------------------

void test_promotion_at_equal_version_accepted(void) {
  TEST_ASSERT_TRUE(otaAcceptable("standard-beta", 100, "standard-stable", 100));
}

void test_promotion_stable_newer_accepted(void) {
  TEST_ASSERT_TRUE(otaAcceptable("standard-beta", 99, "standard-stable", 100));
}

void test_snafu_intra_beta_upgrade_accepted(void) {
  TEST_ASSERT_TRUE(otaAcceptable("snafu-beta", 100, "snafu-beta", 101));
}

void test_snafu_promotion_at_equal_version_accepted(void) {
  TEST_ASSERT_TRUE(otaAcceptable("snafu-beta", 100, "snafu-stable", 100));
}

// --- Promotion reject -------------------------------------------------------

void test_promotion_stable_older_than_beta_rejected(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-beta", 101, "standard-stable", 100));
}

// --- Reverse direction (stable <- beta) reject ------------------------------

void test_stable_receives_beta_rejected(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-stable", 100, "standard-beta", 101));
}

// --- Cross-variant reject ---------------------------------------------------

void test_cross_variant_same_channel_rejected(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-beta", 100, "snafu-beta", 101));
}

void test_cross_variant_promotion_rejected(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-beta", 100, "snafu-stable", 100));
}

// --- Dev isolation (island; dev never mixes with beta/stable) ---------------

void test_dev_rejects_beta_offer(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-dev", 100, "standard-beta", 101));
}

void test_dev_rejects_stable_offer(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-dev", 100, "standard-stable", 101));
}

void test_beta_rejects_dev_offer(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-beta", 100, "standard-dev", 101));
}

void test_stable_rejects_dev_offer(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-stable", 100, "standard-dev", 101));
}

// --- Empty / unknown channel reject -----------------------------------------

void test_empty_offer_channel_rejected(void) {
  TEST_ASSERT_FALSE(otaAcceptable("standard-beta", 100, "", 101));
}

void test_empty_our_channel_rejected(void) {
  TEST_ASSERT_FALSE(otaAcceptable("", 100, "standard-stable", 100));
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_intra_beta_upgrade_accepted);
  RUN_TEST(test_intra_stable_upgrade_accepted);
  RUN_TEST(test_intra_beta_equal_version_rejected);
  RUN_TEST(test_intra_stable_equal_version_rejected);
  RUN_TEST(test_intra_beta_downgrade_rejected);
  RUN_TEST(test_promotion_at_equal_version_accepted);
  RUN_TEST(test_promotion_stable_newer_accepted);
  RUN_TEST(test_snafu_intra_beta_upgrade_accepted);
  RUN_TEST(test_snafu_promotion_at_equal_version_accepted);
  RUN_TEST(test_promotion_stable_older_than_beta_rejected);
  RUN_TEST(test_stable_receives_beta_rejected);
  RUN_TEST(test_cross_variant_same_channel_rejected);
  RUN_TEST(test_cross_variant_promotion_rejected);
  RUN_TEST(test_dev_rejects_beta_offer);
  RUN_TEST(test_dev_rejects_stable_offer);
  RUN_TEST(test_beta_rejects_dev_offer);
  RUN_TEST(test_stable_rejects_dev_offer);
  RUN_TEST(test_empty_offer_channel_rejected);
  RUN_TEST(test_empty_our_channel_rejected);

  return UNITY_END();
}
