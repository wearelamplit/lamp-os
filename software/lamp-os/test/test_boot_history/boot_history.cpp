#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "../../src/core/boot_history.cpp"

using namespace lamp::boot_history;
using lamp::crash_record::Snapshot;

void setUp(void) {}
void tearDown(void) {}

static uint32_t fwOf(uint8_t major, uint8_t minor, uint8_t patch) {
  return (uint32_t{major} << 16) | (uint32_t{minor} << 8) | patch;
}

static Snapshot snapshot(uint32_t uptimeMs, uint32_t minFree, uint32_t minLargest) {
  Snapshot s{};
  s.uptimeMs = uptimeMs;
  s.minFreeHeap = minFree;
  s.minLargestBlock = minLargest;
  return s;
}

static History historyOf(size_t boots) {
  History h{};
  h.version = kVersion;
  for (size_t i = 0; i < boots; ++i) {
    append(h, fwOf(1, 3, static_cast<uint8_t>(i + 1)), ESP_RST_SW);
  }
  return h;
}

void test_append_keeps_the_newest_boots_oldest_first() {
  History h = historyOf(7);
  TEST_ASSERT_EQUAL_UINT16(kBoots, h.count);
  for (size_t i = 0; i < kBoots; ++i) {
    TEST_ASSERT_EQUAL_UINT8(1, h.entries[i].fw[0]);
    TEST_ASSERT_EQUAL_UINT8(3, h.entries[i].fw[1]);
    TEST_ASSERT_EQUAL_UINT8(i + 3, h.entries[i].fw[2]);
  }
}

void test_append_starts_the_new_boot_blank() {
  History h = historyOf(kBoots);
  for (size_t i = 0; i < kBoots; ++i) {
    h.entries[i].uptime = 1234 | kUptimeExact;
    h.entries[i].minFreeHeap = 2000;
    h.entries[i].minLargestBlock = 1000;
  }
  append(h, fwOf(2, 0, 1), ESP_RST_PANIC);
  const BootEntry& e = h.entries[kBoots - 1];
  TEST_ASSERT_EQUAL_UINT32(0, e.uptime);
  TEST_ASSERT_EQUAL_UINT16(0, e.minFreeHeap);
  TEST_ASSERT_EQUAL_UINT16(0, e.minLargestBlock);
  TEST_ASSERT_EQUAL_UINT8(ESP_RST_PANIC, e.resetReason);
  TEST_ASSERT_EQUAL_UINT8(2, e.fw[0]);
  TEST_ASSERT_EQUAL_UINT8(1, e.fw[2]);
}

static void setFigures(BootEntry& e, uint32_t uptime, uint16_t minFree, uint16_t minLargest) {
  e.uptime = uptime;
  e.minFreeHeap = minFree;
  e.minLargestBlock = minLargest;
}

void test_finish_takes_exact_figures_from_the_rtc() {
  History h = historyOf(3);
  setFigures(h.entries[1], 7, 30000, 20000);
  setFigures(h.entries[2], 60, 30000, 20000);
  const BootEntry before = h.entries[1];
  const Snapshot rtc = snapshot(5412345, 70000, 1300);
  finish(h, &rtc);
  const BootEntry& e = h.entries[2];
  TEST_ASSERT_EQUAL_UINT32(5412 | kUptimeExact, e.uptime);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, e.minFreeHeap);
  TEST_ASSERT_EQUAL_UINT16(1300, e.minLargestBlock);
  TEST_ASSERT_EQUAL_MEMORY(&before, &h.entries[1], sizeof(BootEntry));
}

void test_finish_without_rtc_keeps_the_milestone() {
  History h = historyOf(2);
  setFigures(h.entries[1], 3601, 25000, 9000);
  finish(h, nullptr);
  const BootEntry& e = h.entries[1];
  TEST_ASSERT_EQUAL_UINT32(3601, e.uptime);
  TEST_ASSERT_EQUAL_UINT16(25000, e.minFreeHeap);
  TEST_ASSERT_EQUAL_UINT16(9000, e.minLargestBlock);
}

void test_finish_on_an_empty_history_adds_nothing() {
  History h = historyOf(0);
  const Snapshot rtc = snapshot(5000, 100, 100);
  finish(h, &rtc);
  TEST_ASSERT_EQUAL_UINT16(0, h.count);
  TEST_ASSERT_EQUAL_UINT32(0, h.entries[0].uptime);
}

// The panic handler stamps the fault's uptime even when the loop never
// sampled, so the re-arm's setup-time heap figures must still be refused.
void test_finish_ignores_a_boot_that_died_before_its_loop_sampled() {
  struct Case {
    uint32_t uptimeMs;
    uint8_t flags;
  };
  for (const Case c : {Case{0, lamp::crash_record::kUnsampled},
                       Case{5900, lamp::crash_record::kUnsampled}, Case{0, 0}}) {
    History h = historyOf(2);
    Snapshot rearm = snapshot(c.uptimeMs, 120000, 110000);
    rearm.flags = c.flags;
    finish(h, &rearm);
    const BootEntry& e = h.entries[1];
    TEST_ASSERT_EQUAL_UINT32(0, e.uptime);
    TEST_ASSERT_EQUAL_UINT16(0, e.minFreeHeap);
    TEST_ASSERT_EQUAL_UINT16(0, e.minLargestBlock);
  }
}

void test_finish_marks_a_saturated_uptime_as_a_lower_bound() {
  History h = historyOf(2);
  const Snapshot rtc = snapshot(lamp::crash_record::kUptimeSaturated, 20000, 9000);
  finish(h, &rtc);
  TEST_ASSERT_EQUAL_UINT32(lamp::crash_record::kUptimeSaturated / 1000, h.entries[1].uptime);
}

void test_milestone_records_the_milestone_reached() {
  History h = historyOf(2);
  uint8_t next = 0;
  const Snapshot now = snapshot(3599800, 41000, 33000);
  TEST_ASSERT_FALSE(milestoneUpdate(h, next, 59999, &now));
  TEST_ASSERT_TRUE(milestoneUpdate(h, next, 60250, &now));
  TEST_ASSERT_EQUAL_UINT32(60, h.entries[1].uptime);
  TEST_ASSERT_EQUAL_UINT16(41000, h.entries[1].minFreeHeap);
  TEST_ASSERT_EQUAL_UINT16(33000, h.entries[1].minLargestBlock);
  TEST_ASSERT_EQUAL_UINT32(0, h.entries[0].uptime);
  TEST_ASSERT_FALSE(milestoneUpdate(h, next, 61000, &now));
  TEST_ASSERT_TRUE(milestoneUpdate(h, next, 3600100, &now));
  TEST_ASSERT_EQUAL_UINT32(3600, h.entries[1].uptime);
}

void test_milestone_needs_a_boot_and_a_snapshot() {
  uint8_t next = 0;
  const Snapshot now = snapshot(61000, 41000, 33000);
  History empty = historyOf(0);
  TEST_ASSERT_FALSE(milestoneUpdate(empty, next, 61000, &now));
  TEST_ASSERT_EQUAL_UINT32(0, empty.entries[0].uptime);
  History h = historyOf(1);
  TEST_ASSERT_FALSE(milestoneUpdate(h, next, 61000, nullptr));
  TEST_ASSERT_EQUAL_UINT32(0, h.entries[0].uptime);
}

void test_milestones_fire_once_each() {
  uint8_t next = 0;
  TEST_ASSERT_FALSE(milestoneDue(59999, next));
  TEST_ASSERT_TRUE(milestoneDue(60000, next));
  TEST_ASSERT_FALSE(milestoneDue(60001, next));
  TEST_ASSERT_FALSE(milestoneDue(3599999, next));
  TEST_ASSERT_TRUE(milestoneDue(3600000, next));
  TEST_ASSERT_FALSE(milestoneDue(86399999, next));
  TEST_ASSERT_TRUE(milestoneDue(86400000, next));
  TEST_ASSERT_FALSE(milestoneDue(200000000, next));
}

void test_a_late_first_check_writes_once() {
  uint8_t next = 0;
  TEST_ASSERT_TRUE(milestoneDue(90000000, next));
  TEST_ASSERT_FALSE(milestoneDue(90001000, next));
}

void test_line_reads_the_end_from_the_next_boot() {
  History h = historyOf(4);
  h.entries[0].uptime = 42 | kUptimeExact;
  h.entries[0].minFreeHeap = 21480;
  h.entries[0].minLargestBlock = 1300;
  h.entries[1].resetReason = ESP_RST_PANIC;
  h.entries[1].uptime = 3600;
  h.entries[1].minFreeHeap = 30112;
  h.entries[1].minLargestBlock = 18200;
  h.entries[2].resetReason = ESP_RST_POWERON;
  h.entries[2].uptime = 5 * 3600 + 12 * 60 + 9;
  h.entries[3].resetReason = ESP_RST_TASK_WDT;
  char line[96];
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_EQUAL_STRING("[boots] -3 fw1.3.1 up 42s free 21480 big 1300 end panic", line);
  formatLine(line, sizeof(line), h, 1);
  TEST_ASSERT_EQUAL_STRING("[boots] -2 fw1.3.2 up >1h00m free 30112 big 18200 end power", line);
  formatLine(line, sizeof(line), h, 2);
  TEST_ASSERT_EQUAL_STRING("[boots] -1 fw1.3.3 up >5h12m free 0 big 0 end taskwdt", line);
}

void test_line_marks_a_boot_with_no_figures() {
  History h = historyOf(2);
  h.entries[1].resetReason = ESP_RST_POWERON;
  char line[96];
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_EQUAL_STRING("[boots] -1 fw1.3.1 up >0s free - big - end power", line);
}

void test_line_uptime_units() {
  History h = historyOf(2);
  char line[96];
  h.entries[0].uptime = (12 * 60 + 5) | kUptimeExact;
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " up 12m05s "));
  h.entries[0].uptime = (3 * 86400 + 4 * 3600 + 59) | kUptimeExact;
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " up 3d04h "));
}

void test_line_names_an_unknown_reset() {
  History h = historyOf(2);
  h.entries[1].resetReason = 200;
  char line[96];
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " end ?"));
}

void test_loaded_rejects_a_foreign_blob() {
  History h = historyOf(3);
  TEST_ASSERT_TRUE(loaded(h, sizeof(History)));
  TEST_ASSERT_FALSE(loaded(h, sizeof(History) - sizeof(BootEntry)));
  h.version = kVersion + 1;
  TEST_ASSERT_FALSE(loaded(h, sizeof(History)));
  h.version = kVersion;
  h.count = kBoots + 1;
  TEST_ASSERT_FALSE(loaded(h, sizeof(History)));
}

void test_the_running_boot_has_not_ended() {
  TEST_ASSERT_EQUAL_UINT32(0, endedCount(historyOf(0)));
  TEST_ASSERT_EQUAL_UINT32(0, endedCount(historyOf(1)));
  TEST_ASSERT_EQUAL_UINT32(3, endedCount(historyOf(4)));
  TEST_ASSERT_EQUAL_UINT32(kBoots - 1, endedCount(historyOf(9)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_append_keeps_the_newest_boots_oldest_first);
  RUN_TEST(test_append_starts_the_new_boot_blank);
  RUN_TEST(test_finish_takes_exact_figures_from_the_rtc);
  RUN_TEST(test_finish_without_rtc_keeps_the_milestone);
  RUN_TEST(test_finish_on_an_empty_history_adds_nothing);
  RUN_TEST(test_finish_ignores_a_boot_that_died_before_its_loop_sampled);
  RUN_TEST(test_finish_marks_a_saturated_uptime_as_a_lower_bound);
  RUN_TEST(test_milestone_records_the_milestone_reached);
  RUN_TEST(test_milestone_needs_a_boot_and_a_snapshot);
  RUN_TEST(test_milestones_fire_once_each);
  RUN_TEST(test_a_late_first_check_writes_once);
  RUN_TEST(test_line_reads_the_end_from_the_next_boot);
  RUN_TEST(test_line_marks_a_boot_with_no_figures);
  RUN_TEST(test_line_uptime_units);
  RUN_TEST(test_line_names_an_unknown_reset);
  RUN_TEST(test_loaded_rejects_a_foreign_blob);
  RUN_TEST(test_the_running_boot_has_not_ended);
  return UNITY_END();
}
