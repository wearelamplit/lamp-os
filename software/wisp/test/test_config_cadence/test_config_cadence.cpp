#include <unity.h>

#include "status/config_cadence.hpp"

using wisp::ConfigCadence;

void setUp() {}
void tearDown() {}

// The boot config goes out once, kGapMs after the first status, with no burst.
void test_boot_config_sent_once_after_gap() {
  ConfigCadence c;
  c.onStatusEmitted(1000, 0xAAAA);
  TEST_ASSERT_FALSE(c.configDue(1000));
  TEST_ASSERT_FALSE(c.configDue(1000 + ConfigCadence::kGapMs - 1));
  TEST_ASSERT_TRUE(c.configDue(1000 + ConfigCadence::kGapMs));
  c.onConfigEmitted(1250);

  c.onStatusEmitted(3250, 0xAAAA);
  TEST_ASSERT_FALSE(c.configDue(3500));
  c.onStatusEmitted(33250, 0xAAAA);
  TEST_ASSERT_FALSE(c.configDue(33500));
}

// A status never follows a config inside the gap, so a lamp's single inbound
// control-op slot never holds both.
void test_status_held_off_inside_gap_after_config() {
  ConfigCadence c;
  TEST_ASSERT_TRUE(c.statusClear(0));
  c.poll();
  TEST_ASSERT_TRUE(c.configDue(5000));
  c.onConfigEmitted(5000);
  TEST_ASSERT_FALSE(c.statusClear(5000 + ConfigCadence::kGapMs - 1));
  TEST_ASSERT_TRUE(c.statusClear(5000 + ConfigCadence::kGapMs));
}

// A changed fingerprint sends the config after that status, and again after
// every status for the rest of the burst, then stops.
void test_change_bursts_config_with_status() {
  ConfigCadence c;
  c.onStatusEmitted(0, 1);
  c.onConfigEmitted(300);

  c.onStatusEmitted(10000, 2);
  TEST_ASSERT_TRUE(c.configDue(10250));
  c.onConfigEmitted(10250);

  c.onStatusEmitted(12000, 2);
  TEST_ASSERT_FALSE(c.configDue(12100));
  TEST_ASSERT_TRUE(c.configDue(12250));
  c.onConfigEmitted(12250);

  const uint32_t afterBurst = 10000 + ConfigCadence::kBurstMs + 2000;
  c.onStatusEmitted(afterBurst, 2);
  TEST_ASSERT_FALSE(c.configDue(afterBurst + 1000));
}

// pollConfig is coalesced: inside kMinIntervalMs of the last config the poll
// is deferred, then served, never dropped.
void test_poll_coalesced_then_served() {
  ConfigCadence c;
  c.poll();
  TEST_ASSERT_TRUE(c.configDue(100));
  c.onConfigEmitted(100);
  TEST_ASSERT_FALSE(c.configDue(200));

  c.poll();
  c.poll();
  TEST_ASSERT_FALSE(c.configDue(100 + ConfigCadence::kMinIntervalMs - 1));
  TEST_ASSERT_TRUE(c.configDue(100 + ConfigCadence::kMinIntervalMs));
  c.onConfigEmitted(100 + ConfigCadence::kMinIntervalMs);
  TEST_ASSERT_FALSE(c.configDue(100 + 3 * ConfigCadence::kMinIntervalMs));
}

// A config sent once, then none for a whole wrap: the next status goes out
// and a polled config is served after only the status gap.
void test_old_config_holds_nothing_a_wrap_later(void) {
  ConfigCadence c;
  c.poll();
  c.onConfigEmitted(1000);
  for (uint64_t t = 1000; t < 1000 + (1ull << 32); t += 1ull << 28) {
    c.retire(static_cast<uint32_t>(t));
  }
  TEST_ASSERT_TRUE(c.statusClear(1000 + 10));
  c.poll();
  TEST_ASSERT_TRUE(c.configDue(1000 + 10));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_config_sent_once_after_gap);
  RUN_TEST(test_status_held_off_inside_gap_after_config);
  RUN_TEST(test_change_bursts_config_with_status);
  RUN_TEST(test_poll_coalesced_then_served);
  RUN_TEST(test_old_config_holds_nothing_a_wrap_later);
  return UNITY_END();
}
