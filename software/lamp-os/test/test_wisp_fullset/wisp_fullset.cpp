// Full-set (100-entry) MSG_WISP_PAINT / MSG_WISP_CLAIM build+parse round-trip.
// The v2 frame carries the whole claim/paint set in one broadcast (no
// windowing); these pin the caps + the round-trip at the maximum entry count.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include <lampos/protocol/wisp.hpp>

namespace lp = lamp_protocol;

void setUp(void) {}
void tearDown(void) {}

namespace {
const uint8_t kSrc[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
}  // namespace

void test_frame_caps() {
  TEST_ASSERT_EQUAL_UINT32(100, lp::WISP_PAINT_MAX_ENTRIES);
  TEST_ASSERT_EQUAL_UINT32(100, lp::kMaxWispClaimEntries);
  TEST_ASSERT_EQUAL_UINT32(1213, lp::WISP_PAINT_MAX_SIZE);
  TEST_ASSERT_EQUAL_UINT32(713, lp::WISP_CLAIM_MAX_SIZE);
  TEST_ASSERT_TRUE(lp::WISP_PAINT_MAX_SIZE <= lp::ESPNOW_V2_FRAME_MAX);
  TEST_ASSERT_TRUE(lp::WISP_CLAIM_MAX_SIZE <= lp::ESPNOW_V2_FRAME_MAX);
}

void test_paint_fullset_round_trip() {
  uint8_t entries[lp::WISP_PAINT_MAX_ENTRIES * lp::WISP_PAINT_ENTRY_SIZE];
  for (size_t i = 0; i < sizeof(entries); ++i) {
    entries[i] = static_cast<uint8_t>(i * 7 + 3);
  }
  uint8_t buf[lp::WISP_PAINT_MAX_SIZE];
  const size_t n = lp::buildWispPaint(buf, sizeof(buf), /*seq=*/9, kSrc,
                                      entries, lp::WISP_PAINT_MAX_ENTRIES);
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_PAINT_MAX_SIZE, n);

  lp::ParsedWispPaint out;
  TEST_ASSERT_TRUE(lp::parseWispPaint(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(lp::WISP_PAINT_MAX_ENTRIES, out.count);
  TEST_ASSERT_EQUAL_UINT16(9, out.seq);
  TEST_ASSERT_EQUAL_MEMORY(kSrc, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_MEMORY(entries, out.entries, sizeof(entries));
}

void test_claim_fullset_round_trip() {
  uint8_t entries[lp::kMaxWispClaimEntries * lp::WISP_CLAIM_ENTRY_SIZE];
  for (size_t i = 0; i < sizeof(entries); ++i) {
    entries[i] = static_cast<uint8_t>(i * 5 + 1);
  }
  uint8_t buf[lp::WISP_CLAIM_MAX_SIZE];
  const size_t n = lp::buildWispClaim(buf, sizeof(buf), /*seq=*/11, kSrc,
                                      entries, lp::kMaxWispClaimEntries);
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_CLAIM_MAX_SIZE, n);

  lp::ParsedWispClaim out;
  TEST_ASSERT_TRUE(lp::parseWispClaim(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(lp::kMaxWispClaimEntries, out.count);
  TEST_ASSERT_EQUAL_UINT16(11, out.seq);
  TEST_ASSERT_EQUAL_MEMORY(kSrc, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_MEMORY(entries, out.entries, sizeof(entries));
}

void test_paint_over_cap_rejected() {
  uint8_t entries[lp::WISP_PAINT_ENTRY_SIZE] = {0};
  uint8_t buf[lp::WISP_PAINT_MAX_SIZE];
  const size_t n = lp::buildWispPaint(buf, sizeof(buf), 1, kSrc, entries,
                                      lp::WISP_PAINT_MAX_ENTRIES + 1);
  TEST_ASSERT_EQUAL_UINT32(0, n);
}

void test_state_frame_caps() {
  TEST_ASSERT_EQUAL_UINT32(120, lp::WISP_STATE_MAX_ENTRIES);
  TEST_ASSERT_EQUAL_UINT32(12, lp::WISP_STATE_ENTRY_SIZE);
  TEST_ASSERT_EQUAL_UINT32(19, lp::WISP_STATE_FIXED_PREFIX);
  TEST_ASSERT_EQUAL_UINT32(1459, lp::WISP_STATE_MAX_SIZE);
  TEST_ASSERT_TRUE(lp::WISP_STATE_MAX_SIZE <= lp::ESPNOW_V2_FRAME_MAX);
}

void test_state_fullset_round_trip() {
  uint8_t entries[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_ENTRY_SIZE];
  for (size_t i = 0; i < sizeof(entries); ++i) {
    entries[i] = static_cast<uint8_t>(i * 7 + 3);
  }
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), /*seq=*/42, kSrc,
                                      /*brightness=*/200,
                                      /*driftRateMs=*/1234567,
                                      /*presenceFlags=*/0x03, entries,
                                      lp::WISP_STATE_MAX_ENTRIES);
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_STATE_MAX_SIZE, n);

  lp::ParsedWispState out;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(lp::WISP_STATE_MAX_ENTRIES, out.count);
  TEST_ASSERT_EQUAL_UINT16(42, out.seq);
  TEST_ASSERT_EQUAL_MEMORY(kSrc, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8(200, out.brightness);
  TEST_ASSERT_EQUAL_UINT32(1234567, out.driftRateMs);
  TEST_ASSERT_EQUAL_UINT8(0x03, out.presenceFlags);
  TEST_ASSERT_EQUAL_MEMORY(entries, out.entries, sizeof(entries));
}

void test_state_zero_entries_round_trip() {
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), /*seq=*/7, kSrc,
                                      /*brightness=*/64,
                                      /*driftRateMs=*/60000,
                                      /*presenceFlags=*/0x00, nullptr, 0);
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_STATE_FIXED_PREFIX, n);

  lp::ParsedWispState out;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(0, out.count);
  TEST_ASSERT_EQUAL_UINT16(7, out.seq);
  TEST_ASSERT_EQUAL_UINT8(64, out.brightness);
  TEST_ASSERT_EQUAL_UINT32(60000, out.driftRateMs);
  TEST_ASSERT_EQUAL_UINT8(0x00, out.presenceFlags);
  TEST_ASSERT_NULL(out.entries);
}

void test_state_globals_survive_independent_of_count() {
  uint8_t entries[3 * lp::WISP_STATE_ENTRY_SIZE];
  for (size_t i = 0; i < sizeof(entries); ++i) entries[i] = static_cast<uint8_t>(i);
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), /*seq=*/5, kSrc,
                                      /*brightness=*/255,
                                      /*driftRateMs=*/3600000,
                                      /*presenceFlags=*/0xAA, entries, 3);
  TEST_ASSERT_TRUE(n > 0);

  lp::ParsedWispState out;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(3, out.count);
  TEST_ASSERT_EQUAL_UINT8(255, out.brightness);
  TEST_ASSERT_EQUAL_UINT32(3600000, out.driftRateMs);
  TEST_ASSERT_EQUAL_UINT8(0xAA, out.presenceFlags);
}

void test_state_over_cap_rejected() {
  uint8_t entries[lp::WISP_STATE_ENTRY_SIZE] = {0};
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 0, 0, 0, entries,
                                      lp::WISP_STATE_MAX_ENTRIES + 1);
  TEST_ASSERT_EQUAL_UINT32(0, n);
}

void test_state_truncated_rejected() {
  uint8_t entries[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_ENTRY_SIZE] = {0};
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 0, 0, 0, entries,
                                      lp::WISP_STATE_MAX_ENTRIES);
  TEST_ASSERT_TRUE(n > 0);
  lp::ParsedWispState out;
  // A frame shorter than the fixed prefix, and one covering the prefix but not
  // the declared entries, must both reject with no over-read.
  TEST_ASSERT_FALSE(lp::parseWispState(buf, 10, out));
  TEST_ASSERT_FALSE(lp::parseWispState(buf, 30, out));
}

void test_state_find_own_entry_picks_base_shade() {
  const uint8_t kMine[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
  uint8_t entries[3 * lp::WISP_STATE_ENTRY_SIZE];
  // Entry 0 + 2 are other lamps; entry 1 is ours with distinct base/shade.
  for (size_t i = 0; i < sizeof(entries); ++i) entries[i] = static_cast<uint8_t>(0x80 + i);
  uint8_t* mine = &entries[1 * lp::WISP_STATE_ENTRY_SIZE];
  std::memcpy(mine, kMine, 6);
  mine[6] = 200; mine[7] = 100; mine[8] = 50;    // baseRGB
  mine[9] = 25;  mine[10] = 75; mine[11] = 175;  // shadeRGB

  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 64, 8000, 0, entries, 3);
  TEST_ASSERT_TRUE(n > 0);
  lp::ParsedWispState out;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, out));

  const uint8_t* e = lp::findWispStateEntry(out.entries, out.count, kMine);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_UINT8(200, e[6]);
  TEST_ASSERT_EQUAL_UINT8(100, e[7]);
  TEST_ASSERT_EQUAL_UINT8(50,  e[8]);
  TEST_ASSERT_EQUAL_UINT8(25,  e[9]);
  TEST_ASSERT_EQUAL_UINT8(75,  e[10]);
  TEST_ASSERT_EQUAL_UINT8(175, e[11]);
}

void test_state_find_own_entry_absent_returns_null() {
  const uint8_t kMine[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
  uint8_t entries[3 * lp::WISP_STATE_ENTRY_SIZE];
  for (size_t i = 0; i < sizeof(entries); ++i) entries[i] = static_cast<uint8_t>(i);
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 64, 8000, 0, entries, 3);
  TEST_ASSERT_TRUE(n > 0);
  lp::ParsedWispState out;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, out));
  TEST_ASSERT_NULL(lp::findWispStateEntry(out.entries, out.count, kMine));
  // Zero-entry frame: nothing to match.
  TEST_ASSERT_NULL(lp::findWispStateEntry(nullptr, 0, kMine));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_frame_caps);
  RUN_TEST(test_paint_fullset_round_trip);
  RUN_TEST(test_claim_fullset_round_trip);
  RUN_TEST(test_paint_over_cap_rejected);
  RUN_TEST(test_state_frame_caps);
  RUN_TEST(test_state_fullset_round_trip);
  RUN_TEST(test_state_zero_entries_round_trip);
  RUN_TEST(test_state_globals_survive_independent_of_count);
  RUN_TEST(test_state_over_cap_rejected);
  RUN_TEST(test_state_truncated_rejected);
  RUN_TEST(test_state_find_own_entry_picks_base_shade);
  RUN_TEST(test_state_find_own_entry_absent_returns_null);
  return UNITY_END();
}
