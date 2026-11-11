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
  s.flags = kPanicked;
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
    TEST_ASSERT_TRUE((rec.snap.flags & kPanicked) != 0);
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
  const char* details = "assert failed: xQueueGenericSend queue.c:820 (pxQueue)";
  Snapshot s{};
  recordPanic(s, details, 0x400d4b6b, nullptr, 0, 1, 1);
  TEST_ASSERT_EQUAL_size_t(kReasonLen - 1, std::strlen(s.reason));
  TEST_ASSERT_EQUAL_INT(0, std::strncmp(details, s.reason, kReasonLen - 1));
}

// abort()'s message carries its caller's address, which would not fit whole.
void test_abort_reason_drops_the_caller_address() {
  for (const char* details : {"abort() was called at PC 0x400d4b6b on core 1",
                              "abort() was called at PC 0x40081f2 on core 0"}) {
    const char* reason = abortReason(details);
    TEST_ASSERT_EQUAL_STRING("abort() was called", reason);
    TEST_ASSERT_LESS_THAN_size_t(kReasonLen, std::strlen(reason));
  }
  const char* other = "***ERROR*** A stack overflow in task loopTask";
  TEST_ASSERT_EQUAL_PTR(other, abortReason(other));
  TEST_ASSERT_NULL(abortReason(nullptr));
}

void test_rearm_reads_unsampled_until_the_first_sample() {
  Snapshot s{};
  s.flags = kPanicked;
  s.uptimeMs = 90000;
  s.rosterPeak = 7;
  rearm(s, 110580, 127848);
  TEST_ASSERT_FALSE(loopSampled(s));
  TEST_ASSERT_EQUAL_INT8(-1, s.core);
  TEST_ASSERT_EQUAL_UINT8(kUnsampled, s.flags);
  TEST_ASSERT_EQUAL_UINT16(0, s.rosterPeak);

  recordPanic(s, nullptr, 0x401306d9, nullptr, 0, 1, 1600);
  TEST_ASSERT_FALSE(loopSampled(s));

  rearm(s, 110580, 127848);
  takeSample(s, 1200, 29684, 28576, 3);
  TEST_ASSERT_TRUE(loopSampled(s));
  takeSample(s, 2200, 31000, 28100, 2);
  TEST_ASSERT_EQUAL_UINT32(29684, s.minLargestBlock);
  TEST_ASSERT_EQUAL_UINT32(28100, s.minFreeHeap);
  TEST_ASSERT_EQUAL_UINT16(3, s.rosterPeak);
  TEST_ASSERT_EQUAL_UINT32(2200, s.uptimeMs);
}

// A snapshot without the flag still reads unsampled at the re-arm's uptime 0.
void test_zero_uptime_reads_unsampled_without_the_flag() {
  Snapshot s{};
  s.flags = kPanicked;
  TEST_ASSERT_FALSE(loopSampled(s));
  s.uptimeMs = 1000;
  TEST_ASSERT_TRUE(loopSampled(s));
}

static JsonObject lastCrashOf(JsonDocument& doc, const Record& rec) {
  emitLastCrash(doc, rec);
  return doc["lastCrash"].as<JsonObject>();
}

static Record taskWdtRecord(uint8_t flags) {
  const unsigned int bt[] = {0x401306d9, 0x40130ee2, 0x40125cfb};
  Record rec{};
  rec.count = 3;
  rec.resetReason = ESP_RST_TASK_WDT;
  rec.fwVersion = 0x010303;
  rec.snapshotValid = 1;
  rec.snap.core = -1;
  rec.snap.minLargestBlock = 29684;
  rec.snap.minFreeHeap = 28576;
  rec.snap.rosterPeak = 4;
  rec.snap.flags = flags;
  recordPanic(rec.snap, nullptr, 0x401306d9, bt, 3, 1, 6500);
  return rec;
}

void test_last_crash_absent_before_any_crash() {
  JsonDocument doc;
  Record rec{};
  emitLastCrash(doc, rec);
  TEST_ASSERT_FALSE(doc["lastCrash"].is<JsonObject>());
}

void test_last_crash_task_watchdog_carries_core_pc_bt_and_uptime() {
  JsonDocument doc;
  const JsonObject o = lastCrashOf(doc, taskWdtRecord(0));
  TEST_ASSERT_EQUAL_UINT32(6500, o["uptimeMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(1, o["core"].as<int>());
  TEST_ASSERT_EQUAL_STRING("0x401306d9", o["pc"].as<const char*>());
  const JsonArray bt = o["bt"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(3, bt.size());
  TEST_ASSERT_EQUAL_STRING("0x40125cfb", bt[2].as<const char*>());
  TEST_ASSERT_FALSE(o["reason"].is<const char*>());
  TEST_ASSERT_EQUAL_UINT32(29684, o["minLargestBlock"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(4, o["rosterPeak"].as<uint32_t>());
}

void test_last_crash_names_a_recorded_reason() {
  JsonDocument doc;
  Record rec = taskWdtRecord(0);
  recordPanic(rec.snap, "LoadProhibited", 0x400d1234, nullptr, 0, 0, 900);
  const JsonObject o = lastCrashOf(doc, rec);
  TEST_ASSERT_EQUAL_STRING("LoadProhibited", o["reason"].as<const char*>());
  TEST_ASSERT_EQUAL_size_t(0, o["bt"].as<JsonArray>().size());
}

void test_last_crash_before_the_loop_sampled_has_no_heap_figures() {
  JsonDocument doc;
  const JsonObject o = lastCrashOf(doc, taskWdtRecord(kUnsampled));
  TEST_ASSERT_EQUAL_UINT32(6500, o["uptimeMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(1, o["core"].as<int>());
  TEST_ASSERT_FALSE(o["minLargestBlock"].is<uint32_t>());
  TEST_ASSERT_FALSE(o["minFreeHeap"].is<uint32_t>());
  TEST_ASSERT_FALSE(o["rosterPeak"].is<uint32_t>());
}

void test_last_crash_without_a_snapshot_has_only_the_reset() {
  JsonDocument doc;
  Record rec = taskWdtRecord(0);
  rec.snapshotValid = 0;
  const JsonObject o = lastCrashOf(doc, rec);
  TEST_ASSERT_EQUAL_UINT32(3, o["count"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(ESP_RST_TASK_WDT, o["reset"].as<int>());
  TEST_ASSERT_FALSE(o["uptimeMs"].is<uint32_t>());
  TEST_ASSERT_FALSE(o["pc"].is<const char*>());
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
  RUN_TEST(test_abort_reason_drops_the_caller_address);
  RUN_TEST(test_rearm_reads_unsampled_until_the_first_sample);
  RUN_TEST(test_zero_uptime_reads_unsampled_without_the_flag);
  RUN_TEST(test_last_crash_absent_before_any_crash);
  RUN_TEST(test_last_crash_task_watchdog_carries_core_pc_bt_and_uptime);
  RUN_TEST(test_last_crash_names_a_recorded_reason);
  RUN_TEST(test_last_crash_before_the_loop_sampled_has_no_heap_figures);
  RUN_TEST(test_last_crash_without_a_snapshot_has_only_the_reset);
  return UNITY_END();
}
