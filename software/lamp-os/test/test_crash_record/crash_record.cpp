#include <unity.h>

#include <cstring>

#include "../../src/core/crash_record.cpp"

using namespace lamp::crash_record;

void setUp(void) {}
void tearDown(void) {}

static Snapshot panickedSnapshot() {
  Snapshot s{};
  std::strcpy(s.reason, "abort()");
  s.pc = 0x400d1234;
  s.core = 0;
  s.panicked = 1;
  s.minLargestBlock = 1300;
  return s;
}

void test_clean_restart_is_not_a_crash() {
  Record rec{};
  Snapshot s{};
  s.core = -1;
  TEST_ASSERT_FALSE(foldBoot(rec, ESP_RST_SW, &s, 0x010207));
  TEST_ASSERT_FALSE(foldBoot(rec, ESP_RST_POWERON, nullptr, 0x010207));
  TEST_ASSERT_EQUAL_UINT32(0, rec.count);
}

void test_panic_reset_records_the_snapshot() {
  Record rec{};
  const Snapshot s = panickedSnapshot();
  TEST_ASSERT_TRUE(foldBoot(rec, ESP_RST_PANIC, &s, 0x010207));
  TEST_ASSERT_EQUAL_UINT32(1, rec.count);
  TEST_ASSERT_EQUAL_UINT8(ESP_RST_PANIC, rec.resetReason);
  TEST_ASSERT_EQUAL_UINT32(0x010207, rec.fwVersion);
  TEST_ASSERT_TRUE(rec.snapshotValid);
  TEST_ASSERT_EQUAL_STRING("abort()", rec.snap.reason);
  TEST_ASSERT_EQUAL_UINT32(1300, rec.snap.minLargestBlock);
}

void test_brownout_with_garbage_rtc_counts_without_a_snapshot() {
  Record rec{};
  rec.count = 4;
  TEST_ASSERT_TRUE(foldBoot(rec, ESP_RST_BROWNOUT, nullptr, 0x010207));
  TEST_ASSERT_EQUAL_UINT32(5, rec.count);
  TEST_ASSERT_FALSE(rec.snapshotValid);
  TEST_ASSERT_EQUAL_INT8(-1, rec.snap.core);
}

void test_captured_panic_counts_under_any_reset_reason() {
  Record rec{};
  const Snapshot s = panickedSnapshot();
  TEST_ASSERT_TRUE(foldBoot(rec, ESP_RST_SW, &s, 0x010207));
  TEST_ASSERT_EQUAL_UINT32(1, rec.count);
}

void test_watchdogs_are_crashes() {
  TEST_ASSERT_TRUE(isCrashReset(ESP_RST_INT_WDT));
  TEST_ASSERT_TRUE(isCrashReset(ESP_RST_TASK_WDT));
  TEST_ASSERT_TRUE(isCrashReset(ESP_RST_WDT));
  TEST_ASSERT_FALSE(isCrashReset(ESP_RST_DEEPSLEEP));
}

void test_uptime_saturates_instead_of_wrapping() {
  TEST_ASSERT_EQUAL_UINT32(0, uptimeMsFrom(999));
  TEST_ASSERT_EQUAL_UINT32(1234, uptimeMsFrom(1234567));
  const int64_t lastMs = int64_t{kUptimeSaturated} - 1;
  TEST_ASSERT_EQUAL_UINT32(kUptimeSaturated - 1, uptimeMsFrom(lastMs * 1000));
  TEST_ASSERT_EQUAL_UINT32(kUptimeSaturated, uptimeMsFrom((lastMs + 1) * 1000));
  TEST_ASSERT_EQUAL_UINT32(kUptimeSaturated, uptimeMsFrom(int64_t{60} * 86400 * 1000000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_clean_restart_is_not_a_crash);
  RUN_TEST(test_panic_reset_records_the_snapshot);
  RUN_TEST(test_brownout_with_garbage_rtc_counts_without_a_snapshot);
  RUN_TEST(test_captured_panic_counts_under_any_reset_reason);
  RUN_TEST(test_watchdogs_are_crashes);
  RUN_TEST(test_uptime_saturates_instead_of_wrapping);
  return UNITY_END();
}
