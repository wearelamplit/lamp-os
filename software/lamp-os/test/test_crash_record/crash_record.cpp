#include <unity.h>

#include <cstring>
#include <initializer_list>

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

// A stalled loop stops the 1 Hz sample, so the snapshot holds the uptime from
// before the stall; the fault must overwrite it with the uptime at the fault.
void test_panic_records_the_fault_with_its_uptime() {
  const unsigned int bt[] = {0x401306d9, 0x40130ee2, 0x40125cfb, 0x4013aa4c, 0x40141e3c};
  for (const uint32_t sampledMs : {0u, 1000u, 61250u}) {
    Snapshot s{};
    s.core = -1;
    s.uptimeMs = sampledMs;
    s.minLargestBlock = 29684;
    const uint32_t faultMs = sampledMs + 4900;
    recordPanic(s, "", 0x401306d9, bt, 5, 1, faultMs);

    Record rec{};
    TEST_ASSERT_TRUE(foldBoot(rec, ESP_RST_TASK_WDT, &s, 0x010303));
    TEST_ASSERT_EQUAL_UINT32(faultMs, rec.snap.uptimeMs);
    TEST_ASSERT_EQUAL_INT8(1, rec.snap.core);
    TEST_ASSERT_EQUAL_HEX32(0x401306d9, rec.snap.pc);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(bt, rec.snap.backtrace, kBacktraceLen);
    TEST_ASSERT_EQUAL_UINT32(29684, rec.snap.minLargestBlock);
    TEST_ASSERT_EQUAL_UINT8(1, rec.snap.panicked);
  }
}

void test_panic_backtrace_shorter_than_the_record_zero_fills() {
  const unsigned int bt[] = {0x400d1234, 0x400d5678};
  Snapshot s{};
  s.backtrace[2] = 0xdeadbeef;
  s.backtrace[3] = 0xdeadbeef;
  recordPanic(s, nullptr, 0x400d1234, bt, 2, 0, 1);
  TEST_ASSERT_EQUAL_HEX32(0x400d5678, s.backtrace[1]);
  TEST_ASSERT_EQUAL_HEX32(0, s.backtrace[2]);
  TEST_ASSERT_EQUAL_HEX32(0, s.backtrace[3]);
  TEST_ASSERT_EQUAL_STRING("", s.reason);
}

void test_panic_reason_truncates_to_the_record() {
  const char* details = "abort() was called at PC 0x400d4b6b on core 1";
  Snapshot s{};
  recordPanic(s, details, 0x400d4b6b, nullptr, 0, 1, 1);
  TEST_ASSERT_EQUAL_size_t(kReasonLen - 1, std::strlen(s.reason));
  TEST_ASSERT_EQUAL_INT(0, std::strncmp(details, s.reason, kReasonLen - 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_clean_restart_is_not_a_crash);
  RUN_TEST(test_panic_reset_records_the_snapshot);
  RUN_TEST(test_brownout_with_garbage_rtc_counts_without_a_snapshot);
  RUN_TEST(test_captured_panic_counts_under_any_reset_reason);
  RUN_TEST(test_watchdogs_are_crashes);
  RUN_TEST(test_uptime_saturates_instead_of_wrapping);
  RUN_TEST(test_panic_records_the_fault_with_its_uptime);
  RUN_TEST(test_panic_backtrace_shorter_than_the_record_zero_fills);
  RUN_TEST(test_panic_reason_truncates_to_the_record);
  return UNITY_END();
}
