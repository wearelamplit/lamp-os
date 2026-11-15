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
  finish(h, &rtc, nullptr);
  const BootEntry& e = h.entries[2];
  TEST_ASSERT_EQUAL_UINT32(5412 | kUptimeExact, e.uptime);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, e.minFreeHeap);
  TEST_ASSERT_EQUAL_UINT16(1300, e.minLargestBlock);
  TEST_ASSERT_EQUAL_MEMORY(&before, &h.entries[1], sizeof(BootEntry));
}

void test_finish_without_rtc_keeps_the_milestone() {
  History h = historyOf(2);
  setFigures(h.entries[1], 3601, 25000, 9000);
  finish(h, nullptr, nullptr);
  const BootEntry& e = h.entries[1];
  TEST_ASSERT_EQUAL_UINT32(3601, e.uptime);
  TEST_ASSERT_EQUAL_UINT16(25000, e.minFreeHeap);
  TEST_ASSERT_EQUAL_UINT16(9000, e.minLargestBlock);
}

void test_finish_on_an_empty_history_adds_nothing() {
  History h = historyOf(0);
  const Snapshot rtc = snapshot(5000, 100, 100);
  finish(h, &rtc, nullptr);
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
    finish(h, &rearm, nullptr);
    const BootEntry& e = h.entries[1];
    TEST_ASSERT_EQUAL_UINT32(0, e.uptime);
    TEST_ASSERT_EQUAL_UINT16(0, e.minFreeHeap);
    TEST_ASSERT_EQUAL_UINT16(0, e.minLargestBlock);
  }
}

void test_finish_marks_a_saturated_uptime_as_a_lower_bound() {
  History h = historyOf(2);
  const Snapshot rtc = snapshot(lamp::crash_record::kUptimeSaturated, 20000, 9000);
  finish(h, &rtc, nullptr);
  TEST_ASSERT_EQUAL_UINT32(lamp::crash_record::kUptimeSaturated / 1000, h.entries[1].uptime);
}

void test_milestone_records_the_milestone_reached() {
  History h = historyOf(2);
  uint8_t next = 0;
  const Snapshot now = snapshot(3599800, 41000, 33000);
  TEST_ASSERT_FALSE(milestoneUpdate(h, next, 59999, &now, nullptr));
  TEST_ASSERT_TRUE(milestoneUpdate(h, next, 60250, &now, nullptr));
  TEST_ASSERT_EQUAL_UINT32(60, h.entries[1].uptime);
  TEST_ASSERT_EQUAL_UINT16(41000, h.entries[1].minFreeHeap);
  TEST_ASSERT_EQUAL_UINT16(33000, h.entries[1].minLargestBlock);
  TEST_ASSERT_EQUAL_UINT32(0, h.entries[0].uptime);
  TEST_ASSERT_FALSE(milestoneUpdate(h, next, 61000, &now, nullptr));
  TEST_ASSERT_TRUE(milestoneUpdate(h, next, 3600100, &now, nullptr));
  TEST_ASSERT_EQUAL_UINT32(3600, h.entries[1].uptime);
}

void test_milestone_needs_a_boot_and_a_snapshot() {
  uint8_t next = 0;
  const Snapshot now = snapshot(61000, 41000, 33000);
  History empty = historyOf(0);
  TEST_ASSERT_FALSE(milestoneUpdate(empty, next, 61000, &now, nullptr));
  TEST_ASSERT_EQUAL_UINT32(0, empty.entries[0].uptime);
  History h = historyOf(1);
  TEST_ASSERT_FALSE(milestoneUpdate(h, next, 61000, nullptr, nullptr));
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

static void ranToLoop(History& h) {
  for (size_t i = 0; i < h.count; ++i) {
    h.entries[i].stage = static_cast<uint8_t>(Stage::Loop);
    h.entries[i].flags = kStageExact | kCounted;
  }
}

void test_line_reads_the_end_from_the_next_boot() {
  History h = historyOf(4);
  ranToLoop(h);
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
  char line[kLineCap];
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_EQUAL_STRING(
      "[boots] -3 fw1.3.1 up 42s free 21480 big 1300 end panic refused 0 conn 0 connbig -",
      line);
  formatLine(line, sizeof(line), h, 1);
  TEST_ASSERT_EQUAL_STRING(
      "[boots] -2 fw1.3.2 up >1h00m free 30112 big 18200 end power refused 0 conn 0 connbig -",
      line);
  formatLine(line, sizeof(line), h, 2);
  TEST_ASSERT_EQUAL_STRING(
      "[boots] -1 fw1.3.3 up >5h12m free 0 big 0 end taskwdt refused 0 conn 0 connbig -", line);
}

void test_line_marks_a_boot_with_no_figures() {
  History h = historyOf(2);
  h.entries[1].resetReason = ESP_RST_POWERON;
  char line[kLineCap];
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_EQUAL_STRING(
      "[boots] -1 fw1.3.1 up >0s free - big - end power setup >boot refused - conn - connbig -",
      line);
}

void test_line_uptime_units() {
  History h = historyOf(2);
  char line[kLineCap];
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
  char line[kLineCap];
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " end ?"));
}

void test_decode_round_trips_this_layout() {
  History h = historyOf(3);
  h.entries[1].stage = static_cast<uint8_t>(Stage::Mesh);
  h.entries[1].flags = kStageExact | kCounted;
  h.entries[1].connects = 7;
  History out{};
  TEST_ASSERT_TRUE(decode(&h, sizeof(h), out));
  TEST_ASSERT_EQUAL_MEMORY(&h, &out, sizeof(History));
}

static HistoryV1 v1Of(size_t boots) {
  HistoryV1 v1{};
  v1.version = 1;
  v1.count = static_cast<uint16_t>(boots);
  for (size_t i = 0; i < boots; ++i) {
    v1.entries[i].uptime = (100 + i) | (i % 2 ? kUptimeExact : 0);
    v1.entries[i].minFreeHeap = static_cast<uint16_t>(20000 + i);
    v1.entries[i].minLargestBlock = static_cast<uint16_t>(9000 + i);
    v1.entries[i].fw[0] = 1;
    v1.entries[i].fw[1] = 3;
    v1.entries[i].fw[2] = static_cast<uint8_t>(i + 2);
    v1.entries[i].resetReason = static_cast<uint8_t>(ESP_RST_PANIC + i);
  }
  return v1;
}

void test_decode_migrates_a_version_1_history() {
  const HistoryV1 v1 = v1Of(3);
  History out{};
  TEST_ASSERT_TRUE(decode(&v1, sizeof(v1), out));
  TEST_ASSERT_EQUAL_UINT16(kVersion, out.version);
  TEST_ASSERT_EQUAL_UINT16(3, out.count);
  for (size_t i = 0; i < 3; ++i) {
    const BootEntry& e = out.entries[i];
    TEST_ASSERT_EQUAL_UINT32(v1.entries[i].uptime, e.uptime);
    TEST_ASSERT_EQUAL_UINT16(v1.entries[i].minFreeHeap, e.minFreeHeap);
    TEST_ASSERT_EQUAL_UINT16(v1.entries[i].minLargestBlock, e.minLargestBlock);
    TEST_ASSERT_EQUAL_MEMORY(v1.entries[i].fw, e.fw, 3);
    TEST_ASSERT_EQUAL_UINT8(v1.entries[i].resetReason, e.resetReason);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Unknown), e.stage);
    TEST_ASSERT_EQUAL_UINT8(0, e.flags);
  }
  // A migrated boot prints what its firmware recorded, nothing more.
  char line[kLineCap];
  formatLine(line, sizeof(line), out, 0);
  TEST_ASSERT_EQUAL_STRING("[boots] -2 fw1.3.2 up >1m40s free 20000 big 9000 end intwdt", line);
}

void test_decode_rejects_a_foreign_blob() {
  History out = historyOf(2);
  const History before = out;
  History v2 = historyOf(3);
  for (const size_t len : {size_t{0}, size_t{63}, size_t{65}, sizeof(History) - 1,
                           sizeof(History) + 1}) {
    TEST_ASSERT_FALSE(decode(&v2, len, out));
  }
  v2.version = 1;
  TEST_ASSERT_FALSE(decode(&v2, sizeof(v2), out));
  v2.version = kVersion;
  v2.count = kBoots + 1;
  TEST_ASSERT_FALSE(decode(&v2, sizeof(v2), out));
  HistoryV1 v1 = v1Of(2);
  v1.version = kVersion;
  TEST_ASSERT_FALSE(decode(&v1, sizeof(v1), out));
  v1.version = 1;
  v1.count = kBoots + 1;
  TEST_ASSERT_FALSE(decode(&v1, sizeof(v1), out));
  TEST_ASSERT_EQUAL_MEMORY(&before, &out, sizeof(History));
}

static Live liveOf(uint32_t fw, Stage stage, uint16_t refused, uint16_t connects,
                   uint16_t connMin) {
  Live live = freshLive(fw, ESP_RST_SW);
  live.stored = 1;
  live.stage = static_cast<uint8_t>(stage);
  live.refusedLocal = refused;
  live.connects = connects;
  live.connMinLargest = connMin;
  return live;
}

void test_finish_takes_stage_and_counters_from_the_rtc() {
  History h = historyOf(2);
  const Live live = liveOf(fwOf(1, 3, 2), Stage::Mesh, 4, 9, 14000);
  finish(h, nullptr, &live);
  const BootEntry& e = h.entries[1];
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Mesh), e.stage);
  TEST_ASSERT_EQUAL_UINT8(kStageExact | kCounted, e.flags);
  TEST_ASSERT_EQUAL_UINT16(4, e.refusedLocal);
  TEST_ASSERT_EQUAL_UINT16(9, e.connects);
  TEST_ASSERT_EQUAL_UINT16(14000, e.connMinLargest);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Boot), h.entries[0].stage);
  TEST_ASSERT_EQUAL_UINT8(0, h.entries[0].flags);
}

// An RTC state another firmware left (a reflash, an update, a rollback) is not
// this entry's.
void test_finish_ignores_live_state_from_another_firmware() {
  History h = historyOf(2);
  const Live live = liveOf(fwOf(1, 3, 9), Stage::Mesh, 4, 9, 14000);
  finish(h, nullptr, &live);
  const BootEntry& e = h.entries[1];
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Boot), e.stage);
  TEST_ASSERT_EQUAL_UINT8(0, e.flags);
  TEST_ASSERT_EQUAL_UINT16(0, e.connects);
}

// The boot's entry never reached NVS, so it comes back from the RTC state
// before the fold, and the boot before it keeps its own figures and end.
void test_a_boot_whose_entry_never_stored_is_rebuilt_from_the_rtc() {
  History h = historyOf(2);
  h.entries[1].stage = static_cast<uint8_t>(Stage::Loop);
  h.entries[1].flags = kStageExact | kCounted;
  const BootEntry olderBefore = h.entries[1];
  Live live = freshLive(fwOf(1, 3, 6), ESP_RST_BROWNOUT);
  live.stage = static_cast<uint8_t>(Stage::Ble);
  live.connects = 2;
  const Snapshot rtc = snapshot(5412345, 30000, 1300);
  finish(h, &rtc, &live);
  TEST_ASSERT_EQUAL_UINT16(3, h.count);
  TEST_ASSERT_EQUAL_MEMORY(&olderBefore, &h.entries[1], sizeof(BootEntry));
  const BootEntry& e = h.entries[2];
  TEST_ASSERT_EQUAL_UINT8(6, e.fw[2]);
  TEST_ASSERT_EQUAL_UINT8(ESP_RST_BROWNOUT, e.resetReason);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Ble), e.stage);
  TEST_ASSERT_EQUAL_UINT8(kStageExact | kCounted, e.flags);
  TEST_ASSERT_EQUAL_UINT16(2, e.connects);
  TEST_ASSERT_EQUAL_UINT32(5412 | kUptimeExact, e.uptime);
  append(h, fwOf(1, 3, 6), ESP_RST_TASK_WDT);
  char line[kLineCap];
  formatLine(line, sizeof(line), h, 1);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " end brownout refused 0"));
  formatLine(line, sizeof(line), h, 2);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " fw1.3.6 "));
  TEST_ASSERT_NOT_NULL(std::strstr(line, " end taskwdt setup ble refused 0 conn 2"));
}

// A full history drops its oldest to make room for the rebuilt entry.
void test_a_rebuilt_entry_drops_the_oldest_when_full() {
  History h = historyOf(kBoots);
  Live live = freshLive(fwOf(1, 3, 9), ESP_RST_SW);
  finish(h, nullptr, &live);
  TEST_ASSERT_EQUAL_UINT16(kBoots, h.count);
  TEST_ASSERT_EQUAL_UINT8(2, h.entries[0].fw[2]);
  TEST_ASSERT_EQUAL_UINT8(9, h.entries[kBoots - 1].fw[2]);
  TEST_ASSERT_EQUAL_UINT8(kStageExact | kCounted, h.entries[kBoots - 1].flags);
}

void test_a_connected_sample_is_due_once_a_second_across_the_wrap() {
  constexpr uint32_t kEvery = 1000;
  TEST_ASSERT_FALSE(connSampleDue(false, 0, 5000, kEvery));
  // The first connection of a boot, however late it comes (lastMs still 0).
  TEST_ASSERT_TRUE(connSampleDue(true, 0, 0x80000000u + 5, kEvery));
  TEST_ASSERT_TRUE(connSampleDue(true, 0, UINT32_MAX - 3, kEvery));
  TEST_ASSERT_FALSE(connSampleDue(true, 7000, 7999, kEvery));
  TEST_ASSERT_TRUE(connSampleDue(true, 7000, 8000, kEvery));
  TEST_ASSERT_TRUE(connSampleDue(true, UINT32_MAX - 200, 900, kEvery));
  TEST_ASSERT_FALSE(connSampleDue(true, UINT32_MAX - 200, 700, kEvery));
}

void test_without_the_rtc_the_stage_is_the_last_one_written() {
  History h = historyOf(2);
  finish(h, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Boot), h.entries[1].stage);
  markSetupDone(h);
  finish(h, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Loop), h.entries[1].stage);
  TEST_ASSERT_EQUAL_UINT8(0, h.entries[1].flags & kStageExact);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Boot), h.entries[0].stage);
}

void test_line_names_the_setup_stage() {
  History h = historyOf(2);
  h.entries[1].resetReason = ESP_RST_TASK_WDT;
  char line[kLineCap];
  const Live live = liveOf(fwOf(1, 3, 1), Stage::Mesh, 0, 0, 0);
  History exact = h;
  exact.count = 1;
  finish(exact, nullptr, &live);
  exact.count = 2;
  formatLine(line, sizeof(line), exact, 0);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " end taskwdt setup mesh refused 0 conn 0 connbig -"));
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " end taskwdt setup >boot refused -"));
  h.count = 1;
  markSetupDone(h);
  h.count = 2;
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NULL(std::strstr(line, "setup"));
}

void test_line_prints_counters_and_marks_untaken_ones() {
  History h = historyOf(2);
  BootEntry& e = h.entries[0];
  e.stage = static_cast<uint8_t>(Stage::Loop);
  char line[kLineCap];
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " refused - conn - connbig -"));
  e.flags = kCounted;
  e.refusedLocal = 2;
  e.connects = 3;
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " refused 2 conn 3 connbig -"));
  e.connMinLargest = 11800;
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " refused 2 conn 3 connbig 11800"));
}

void test_the_widest_line_comes_out_whole() {
  History h = historyOf(kBoots);
  BootEntry& e = h.entries[0];
  e.uptime = lamp::crash_record::kUptimeSaturated / 1000;
  e.minFreeHeap = UINT16_MAX;
  e.minLargestBlock = UINT16_MAX;
  e.fw[0] = e.fw[1] = e.fw[2] = 255;
  e.stage = static_cast<uint8_t>(Stage::Behaviors);
  e.flags = kCounted | kUnverified;
  e.refusedLocal = e.connects = e.connMinLargest = UINT16_MAX;
  h.entries[1].resetReason = ESP_RST_BROWNOUT;
  char line[kLineCap];
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_EQUAL_STRING(
      "[boots] -4 fw255.255.255 up >49d17h free 65535 big 65535 end brownout rolledback setup "
      ">behaviors refused 65535 conn 65535 connbig 65535",
      line);
  // No stage name is longer than the one above.
  for (uint8_t i = 0; i <= static_cast<uint8_t>(Stage::Loop); ++i) {
    TEST_ASSERT_TRUE(std::strlen(stageName(static_cast<Stage>(i))) <= std::strlen("behaviors"));
  }
}

void test_an_unverified_boot_is_marked_and_cleared_on_the_running_entry() {
  History h = historyOf(3);
  setUnverified(h, true);
  TEST_ASSERT_EQUAL_UINT8(kUnverified, h.entries[2].flags & kUnverified);
  TEST_ASSERT_EQUAL_UINT8(0, h.entries[1].flags & kUnverified);
  h.entries[2].flags |= kCounted;
  setUnverified(h, false);
  TEST_ASSERT_EQUAL_UINT8(0, h.entries[2].flags & kUnverified);
  TEST_ASSERT_EQUAL_UINT8(kCounted, h.entries[2].flags);
  History empty{};
  setUnverified(empty, true);
  setUnverified(empty, false);
  TEST_ASSERT_EQUAL_UINT16(0, empty.count);
}

void test_an_unverified_boot_followed_by_other_firmware_was_rolled_back() {
  History h{};
  h.version = kVersion;
  append(h, fwOf(1, 3, 7), ESP_RST_POWERON);
  append(h, fwOf(1, 3, 8), ESP_RST_SW);
  setUnverified(h, true);
  append(h, fwOf(1, 3, 7), ESP_RST_TASK_WDT);
  append(h, fwOf(1, 3, 7), ESP_RST_POWERON);
  TEST_ASSERT_FALSE(rolledBack(h, 0));
  TEST_ASSERT_TRUE(rolledBack(h, 1));
  TEST_ASSERT_FALSE(rolledBack(h, 2));
  TEST_ASSERT_FALSE(rolledBack(h, 3));
  char line[kLineCap];
  formatLine(line, sizeof(line), h, 1);
  TEST_ASSERT_NOT_NULL(std::strstr(line, " fw1.3.8 "));
  TEST_ASSERT_NOT_NULL(std::strstr(line, " end taskwdt rolledback setup >boot "));
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NULL(std::strstr(line, "rolledback"));
  formatLine(line, sizeof(line), h, 2);
  TEST_ASSERT_NULL(std::strstr(line, "rolledback"));
}

void test_an_unverified_boot_on_the_same_firmware_was_not_rolled_back() {
  History h{};
  h.version = kVersion;
  append(h, fwOf(1, 3, 8), ESP_RST_SW);
  setUnverified(h, true);
  append(h, fwOf(1, 3, 8), ESP_RST_TASK_WDT);
  TEST_ASSERT_FALSE(rolledBack(h, 0));
  h.entries[1].fw[0] = 2;
  TEST_ASSERT_TRUE(rolledBack(h, 0));
  h.entries[1].fw[0] = 1;
  h.entries[1].fw[1] = 4;
  TEST_ASSERT_TRUE(rolledBack(h, 0));
}

void test_the_running_entry_is_never_judged_rolled_back() {
  History h{};
  h.version = kVersion;
  append(h, fwOf(1, 3, 7), ESP_RST_POWERON);
  append(h, fwOf(1, 3, 8), ESP_RST_SW);
  setUnverified(h, true);
  TEST_ASSERT_FALSE(rolledBack(h, 1));
  TEST_ASSERT_FALSE(rolledBack(h, 2));
}

void test_a_verified_boot_followed_by_other_firmware_was_an_update() {
  History h{};
  h.version = kVersion;
  append(h, fwOf(1, 3, 7), ESP_RST_POWERON);
  setUnverified(h, true);
  setUnverified(h, false);
  append(h, fwOf(1, 3, 8), ESP_RST_SW);
  TEST_ASSERT_FALSE(rolledBack(h, 0));
  char line[kLineCap];
  formatLine(line, sizeof(line), h, 0);
  TEST_ASSERT_NULL(std::strstr(line, "rolledback"));
}

void test_stage_names_round_trip() {
  for (uint8_t i = static_cast<uint8_t>(Stage::Boot); i <= static_cast<uint8_t>(Stage::Loop); ++i) {
    const Stage stage = static_cast<Stage>(i);
    TEST_ASSERT_EQUAL_UINT8(i, static_cast<uint8_t>(stageFromName(stageName(stage))));
  }
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(stageFromName("?")));
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(stageFromName("mesh2")));
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(stageFromName("")));
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(stageFromName(nullptr)));
}

void test_counters_saturate() {
  uint16_t c = UINT16_MAX - 1;
  countUp(c);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, c);
  countUp(c);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, c);
  c = 0;
  countUp(c);
  TEST_ASSERT_EQUAL_UINT16(1, c);
}

void test_connected_low_point_is_the_smallest_sample() {
  Live live = freshLive(fwOf(1, 3, 7), ESP_RST_POWERON);
  TEST_ASSERT_EQUAL_UINT16(0, live.connMinLargest);
  foldConnectedSample(live, 30000);
  TEST_ASSERT_EQUAL_UINT16(30000, live.connMinLargest);
  foldConnectedSample(live, 41000);
  TEST_ASSERT_EQUAL_UINT16(30000, live.connMinLargest);
  foldConnectedSample(live, 12500);
  TEST_ASSERT_EQUAL_UINT16(12500, live.connMinLargest);
  // A zero-byte block still reads as a sample, never as none.
  foldConnectedSample(live, 0);
  TEST_ASSERT_EQUAL_UINT16(1, live.connMinLargest);
  Live big = freshLive(fwOf(1, 3, 7), ESP_RST_POWERON);
  foldConnectedSample(big, 70000);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, big.connMinLargest);
}

void test_fresh_live_state_starts_at_boot() {
  const Live live = freshLive(fwOf(1, 3, 7), ESP_RST_POWERON);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Stage::Boot), live.stage);
  TEST_ASSERT_EQUAL_UINT8(7, live.fw[2]);
  TEST_ASSERT_EQUAL_UINT16(0, live.refusedLocal);
  TEST_ASSERT_EQUAL_UINT16(0, live.connects);
  TEST_ASSERT_EQUAL_UINT8(0, live.stored);
  TEST_ASSERT_EQUAL_UINT8(ESP_RST_POWERON, live.resetReason);
}

void test_milestone_copies_the_counters() {
  History h = historyOf(2);
  uint8_t next = 0;
  const Snapshot now = snapshot(61000, 41000, 33000);
  const Live live = liveOf(fwOf(1, 3, 2), Stage::Loop, 1, 2, 9000);
  TEST_ASSERT_TRUE(milestoneUpdate(h, next, 61000, &now, &live));
  const BootEntry& e = h.entries[1];
  TEST_ASSERT_EQUAL_UINT8(kCounted, e.flags);
  TEST_ASSERT_EQUAL_UINT16(1, e.refusedLocal);
  TEST_ASSERT_EQUAL_UINT16(2, e.connects);
  TEST_ASSERT_EQUAL_UINT16(9000, e.connMinLargest);
  History bare = historyOf(2);
  next = 0;
  TEST_ASSERT_TRUE(milestoneUpdate(bare, next, 61000, &now, nullptr));
  TEST_ASSERT_EQUAL_UINT8(0, bare.entries[1].flags);
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
  RUN_TEST(test_decode_round_trips_this_layout);
  RUN_TEST(test_decode_migrates_a_version_1_history);
  RUN_TEST(test_decode_rejects_a_foreign_blob);
  RUN_TEST(test_finish_takes_stage_and_counters_from_the_rtc);
  RUN_TEST(test_finish_ignores_live_state_from_another_firmware);
  RUN_TEST(test_a_boot_whose_entry_never_stored_is_rebuilt_from_the_rtc);
  RUN_TEST(test_a_rebuilt_entry_drops_the_oldest_when_full);
  RUN_TEST(test_a_connected_sample_is_due_once_a_second_across_the_wrap);
  RUN_TEST(test_without_the_rtc_the_stage_is_the_last_one_written);
  RUN_TEST(test_line_names_the_setup_stage);
  RUN_TEST(test_line_prints_counters_and_marks_untaken_ones);
  RUN_TEST(test_the_widest_line_comes_out_whole);
  RUN_TEST(test_an_unverified_boot_is_marked_and_cleared_on_the_running_entry);
  RUN_TEST(test_an_unverified_boot_followed_by_other_firmware_was_rolled_back);
  RUN_TEST(test_an_unverified_boot_on_the_same_firmware_was_not_rolled_back);
  RUN_TEST(test_the_running_entry_is_never_judged_rolled_back);
  RUN_TEST(test_a_verified_boot_followed_by_other_firmware_was_an_update);
  RUN_TEST(test_stage_names_round_trip);
  RUN_TEST(test_counters_saturate);
  RUN_TEST(test_connected_low_point_is_the_smallest_sample);
  RUN_TEST(test_fresh_live_state_starts_at_boot);
  RUN_TEST(test_milestone_copies_the_counters);
  RUN_TEST(test_the_running_boot_has_not_ended);
  return UNITY_END();
}
