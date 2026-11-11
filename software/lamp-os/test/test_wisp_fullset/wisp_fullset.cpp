// Full-set MSG_WISP_CLAIM / MSG_WISP_STATE build+parse round-trip, and the frame caps.
// The v2 frame carries the whole claim set in one broadcast (no
// windowing); these pin the caps + the round-trip at the maximum entry count,
// and STATE's drift fade trailer: older parsers read past it, its records
// round-trip, and every form fits one frame.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

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

void test_state_frame_caps() {
  TEST_ASSERT_EQUAL_UINT32(12, lp::WISP_STATE_ENTRY_SIZE);
  TEST_ASSERT_EQUAL_UINT32(2, lp::WISP_STATE_W_SIZE);
  TEST_ASSERT_EQUAL_UINT32(19, lp::WISP_STATE_FIXED_PREFIX);
  constexpr size_t kPerEntry = lp::WISP_STATE_ENTRY_SIZE + lp::WISP_STATE_W_SIZE;
  // The cap is the largest count whose full frame, W plane included, fits v2.
  TEST_ASSERT_EQUAL_UINT32(
      lp::WISP_STATE_FIXED_PREFIX + lp::WISP_STATE_MAX_ENTRIES * kPerEntry,
      lp::WISP_STATE_MAX_SIZE);
  TEST_ASSERT_TRUE(lp::WISP_STATE_MAX_SIZE <= lp::ESPNOW_V2_FRAME_MAX);
  TEST_ASSERT_TRUE(lp::WISP_STATE_FIXED_PREFIX +
                       (lp::WISP_STATE_MAX_ENTRIES + 1) * kPerEntry >
                   lp::ESPNOW_V2_FRAME_MAX);
  // Every lamp a wisp can claim fits one frame.
  TEST_ASSERT_TRUE(lp::WISP_STATE_MAX_ENTRIES >= lp::kMaxWispClaimEntries);
}

void test_state_fullset_round_trip() {
  uint8_t entries[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_ENTRY_SIZE];
  for (size_t i = 0; i < sizeof(entries); ++i) {
    entries[i] = static_cast<uint8_t>(i * 7 + 3);
  }
  uint8_t w[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_W_SIZE];
  for (size_t i = 0; i < sizeof(w); ++i) w[i] = static_cast<uint8_t>(i * 5 + 1);
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), /*seq=*/42, kSrc,
                                      /*brightness=*/200,
                                      /*driftRateMs=*/1234567,
                                      /*presenceFlags=*/0x03, entries, w,
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
  TEST_ASSERT_NOT_NULL(out.w);
  TEST_ASSERT_EQUAL_MEMORY(w, out.w, sizeof(w));
}

void test_state_zero_entries_round_trip() {
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), /*seq=*/7, kSrc,
                                      /*brightness=*/64,
                                      /*driftRateMs=*/60000,
                                      /*presenceFlags=*/0x00, nullptr, nullptr, 0);
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_STATE_FIXED_PREFIX, n);

  lp::ParsedWispState out;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(0, out.count);
  TEST_ASSERT_EQUAL_UINT16(7, out.seq);
  TEST_ASSERT_EQUAL_UINT8(64, out.brightness);
  TEST_ASSERT_EQUAL_UINT32(60000, out.driftRateMs);
  TEST_ASSERT_EQUAL_UINT8(0x00, out.presenceFlags);
  TEST_ASSERT_NULL(out.entries);
  TEST_ASSERT_NULL(out.w);
}

void test_state_globals_survive_independent_of_count() {
  uint8_t entries[3 * lp::WISP_STATE_ENTRY_SIZE];
  for (size_t i = 0; i < sizeof(entries); ++i) entries[i] = static_cast<uint8_t>(i);
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), /*seq=*/5, kSrc,
                                      /*brightness=*/255,
                                      /*driftRateMs=*/3600000,
                                      /*presenceFlags=*/0xAA, entries, nullptr, 3);
  TEST_ASSERT_TRUE(n > 0);

  lp::ParsedWispState out;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(3, out.count);
  TEST_ASSERT_EQUAL_UINT8(255, out.brightness);
  TEST_ASSERT_EQUAL_UINT32(3600000, out.driftRateMs);
  TEST_ASSERT_EQUAL_UINT8(0xAA, out.presenceFlags);
}

void test_state_over_cap_rejected() {
  // Inputs and buffer hold count+1 entries with W, so only the count cap rejects.
  constexpr size_t kOver = lp::WISP_STATE_MAX_ENTRIES + 1;
  uint8_t entries[kOver * lp::WISP_STATE_ENTRY_SIZE] = {0};
  uint8_t w[kOver * lp::WISP_STATE_W_SIZE] = {0};
  uint8_t buf[lp::WISP_STATE_FIXED_PREFIX +
              kOver * (lp::WISP_STATE_ENTRY_SIZE + lp::WISP_STATE_W_SIZE)];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 0, 0, 0, entries,
                                      w, kOver);
  TEST_ASSERT_EQUAL_UINT32(0, n);
}

void test_state_truncated_rejected() {
  uint8_t entries[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_ENTRY_SIZE] = {0};
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 0, 0, 0, entries,
                                      nullptr, lp::WISP_STATE_MAX_ENTRIES);
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
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 64, 8000, 0, entries,
                                      nullptr, 3);
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
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 64, 8000, 0, entries,
                                      nullptr, 3);
  TEST_ASSERT_TRUE(n > 0);
  lp::ParsedWispState out;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, out));
  TEST_ASSERT_NULL(lp::findWispStateEntry(out.entries, out.count, kMine));
  // Zero-entry frame: nothing to match.
  TEST_ASSERT_NULL(lp::findWispStateEntry(nullptr, 0, kMine));
}

// The STATE parser shipped in 1.2.7 and 1.2.9, frozen with its constants.
// Deployed lamps run this; it must keep accepting what the wisp now emits.
bool legacyParseWispState(const uint8_t* data, size_t len,
                          lp::ParsedWispState& out) {
  constexpr size_t kPrefix = 19;
  constexpr size_t kEntry = 12;
  constexpr size_t kMaxEntries = 120;
  if (lp::inspect(data, len) != lp::MSG_WISP_STATE) return false;
  if (len < kPrefix) return false;
  const uint8_t count = data[18];
  if (count > kMaxEntries) return false;
  const size_t expected = kPrefix + static_cast<size_t>(count) * kEntry;
  if (len < expected) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.brightness = data[12];
  out.driftRateMs = static_cast<uint32_t>(data[13]) |
                    (static_cast<uint32_t>(data[14]) << 8) |
                    (static_cast<uint32_t>(data[15]) << 16) |
                    (static_cast<uint32_t>(data[16]) << 24);
  out.presenceFlags = data[17];
  out.count = count;
  out.entries = count ? &data[kPrefix] : nullptr;
  return true;
}

void fillPattern(uint8_t* p, size_t n, uint8_t mul, uint8_t add) {
  for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(i * mul + add);
}

void test_state_legacy_parser_reads_w_frame() {
  uint8_t entries[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_ENTRY_SIZE];
  uint8_t w[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_W_SIZE];
  fillPattern(entries, sizeof(entries), 11, 2);
  fillPattern(w, sizeof(w), 3, 9);
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), /*seq=*/0x1234, kSrc,
                                      /*brightness=*/77, /*driftRateMs=*/90000,
                                      /*presenceFlags=*/0x06, entries, w,
                                      lp::WISP_STATE_MAX_ENTRIES);
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_STATE_MAX_SIZE, n);

  lp::ParsedWispState legacy{};
  TEST_ASSERT_TRUE(legacyParseWispState(buf, n, legacy));
  TEST_ASSERT_EQUAL_UINT16(0x1234, legacy.seq);
  TEST_ASSERT_EQUAL_MEMORY(kSrc, legacy.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8(77, legacy.brightness);
  TEST_ASSERT_EQUAL_UINT32(90000, legacy.driftRateMs);
  TEST_ASSERT_EQUAL_UINT8(0x06, legacy.presenceFlags);
  TEST_ASSERT_EQUAL_UINT8(lp::WISP_STATE_MAX_ENTRIES, legacy.count);
  TEST_ASSERT_EQUAL_MEMORY(entries, legacy.entries, sizeof(entries));
}

void test_state_w_plane_round_trip() {
  const uint8_t kMine[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
  uint8_t entries[3 * lp::WISP_STATE_ENTRY_SIZE];
  fillPattern(entries, sizeof(entries), 1, 0x80);
  std::memcpy(&entries[1 * lp::WISP_STATE_ENTRY_SIZE], kMine, 6);
  const uint8_t w[3 * lp::WISP_STATE_W_SIZE] = {1, 2, 0xC8, 0x37, 5, 6};
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 64, 8000, 0,
                                      entries, w, 3);
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_STATE_FIXED_PREFIX +
                               3 * (lp::WISP_STATE_ENTRY_SIZE + lp::WISP_STATE_W_SIZE),
                           n);
  lp::ParsedWispState out;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, out));
  TEST_ASSERT_NOT_NULL(out.w);
  TEST_ASSERT_EQUAL_MEMORY(w, out.w, sizeof(w));
}

void test_state_without_w_plane_parses_with_null_w() {
  uint8_t entries[3 * lp::WISP_STATE_ENTRY_SIZE];
  fillPattern(entries, sizeof(entries), 1, 0);
  const uint8_t w[3 * lp::WISP_STATE_W_SIZE] = {9, 9, 9, 9, 9, 9};
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 64, 8000, 0,
                                      entries, w, 3);
  TEST_ASSERT_TRUE(n > 0);
  lp::ParsedWispState out;
  // An older wisp's frame ends at the entries.
  const size_t legacyLen = n - sizeof(w);
  TEST_ASSERT_TRUE(lp::parseWispState(buf, legacyLen, out));
  TEST_ASSERT_EQUAL_UINT8(3, out.count);
  TEST_ASSERT_NULL(out.w);
  // A partial W plane is not trusted either.
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n - 1, out));
  TEST_ASSERT_NULL(out.w);
}

void test_state_parser_rejects_over_cap_count() {
  // Room for count+1 entries with the W plane, so only the count cap rejects.
  uint8_t buf[lp::WISP_STATE_FIXED_PREFIX +
              (lp::WISP_STATE_MAX_ENTRIES + 1) *
                  (lp::WISP_STATE_ENTRY_SIZE + lp::WISP_STATE_W_SIZE)];
  std::memset(buf, 0, sizeof(buf));
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 0, 0, 0,
                                      nullptr, nullptr, 0);
  TEST_ASSERT_TRUE(n > 0);
  buf[18] = static_cast<uint8_t>(lp::WISP_STATE_MAX_ENTRIES + 1);
  lp::ParsedWispState out;
  TEST_ASSERT_FALSE(lp::parseWispState(buf, sizeof(buf), out));
  buf[18] = static_cast<uint8_t>(lp::WISP_STATE_MAX_ENTRIES);
  TEST_ASSERT_TRUE(lp::parseWispState(buf, sizeof(buf), out));
}

// The STATE parser shipped in 1.3.2, frozen with its constants.
bool parseWispState132(const uint8_t* data, size_t len, lp::ParsedWispState& out) {
  constexpr size_t kPrefix = 19;
  constexpr size_t kEntry = 12;
  constexpr size_t kW = 2;
  constexpr size_t kMaxEntries = 103;
  if (lp::inspect(data, len) != lp::MSG_WISP_STATE) return false;
  if (len < kPrefix) return false;
  const uint8_t count = data[18];
  if (count > kMaxEntries) return false;
  const size_t expected = kPrefix + static_cast<size_t>(count) * kEntry;
  if (len < expected) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.brightness = data[12];
  out.driftRateMs = static_cast<uint32_t>(data[13]) |
                    (static_cast<uint32_t>(data[14]) << 8) |
                    (static_cast<uint32_t>(data[15]) << 16) |
                    (static_cast<uint32_t>(data[16]) << 24);
  out.presenceFlags = data[17];
  out.count = count;
  out.entries = count ? &data[kPrefix] : nullptr;
  out.w = (count && len >= expected + count * kW) ? &data[expected] : nullptr;
  return true;
}

// Record r of `n`: entry index `first + r`, colours and time left all vary.
void fillRecords(uint8_t* recs, size_t n, uint8_t first) {
  for (size_t r = 0; r < n; ++r) {
    uint8_t* p = recs + r * lp::WISP_STATE_DRIFT_FADE_RECORD_SIZE;
    p[0] = static_cast<uint8_t>(first + r);
    for (size_t c = 0; c < 8; ++c) p[1 + c] = static_cast<uint8_t>(r * 13 + c * 29 + 3);
    const uint16_t ds = static_cast<uint16_t>(r * 977 + 1);
    p[9] = static_cast<uint8_t>(ds & 0xFF);
    p[10] = static_cast<uint8_t>(ds >> 8);
  }
}

struct TrailerFrame {
  uint8_t plain[lp::ESPNOW_V2_FRAME_MAX];
  uint8_t full[lp::ESPNOW_V2_FRAME_MAX];
  size_t plainLen;
  size_t fullLen;
};

// `count` entries; `records` of them carry a record, starting at entry 0.
void buildTrailerFrame(TrailerFrame& f, uint8_t count, size_t records,
                       bool partial) {
  static uint8_t entries[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_ENTRY_SIZE];
  static uint8_t w[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_W_SIZE];
  static uint8_t recs[lp::WISP_STATE_DRIFT_FADE_MAX_RECORDS *
                      lp::WISP_STATE_DRIFT_FADE_RECORD_SIZE];
  fillPattern(entries, sizeof(entries), 11, 2);
  fillPattern(w, sizeof(w), 3, 9);
  fillRecords(recs, records, 0);
  f.plainLen = lp::buildWispState(f.plain, sizeof(f.plain), 0x1234, kSrc, 77,
                                  90000, 0x06, entries, w, count);
  std::memcpy(f.full, f.plain, f.plainLen);
  f.fullLen = lp::appendWispStateDriftFade(f.full, sizeof(f.full), f.plainLen,
                                           recs, records, partial);
}

void test_state_132_parser_reads_trailer_frame_unchanged() {
  const uint8_t n = lp::WISP_STATE_DRIFT_FADE_MAX_RECORDS;
  static TrailerFrame f;
  buildTrailerFrame(f, n, n, false);
  TEST_ASSERT_TRUE(f.fullLen > f.plainLen);
  TEST_ASSERT_EQUAL_MEMORY(f.plain, f.full, f.plainLen);

  lp::ParsedWispState plain{};
  lp::ParsedWispState full{};
  TEST_ASSERT_TRUE(parseWispState132(f.plain, f.plainLen, plain));
  TEST_ASSERT_TRUE(parseWispState132(f.full, f.fullLen, full));
  TEST_ASSERT_EQUAL_UINT16(plain.seq, full.seq);
  TEST_ASSERT_EQUAL_UINT8(plain.brightness, full.brightness);
  TEST_ASSERT_EQUAL_UINT32(plain.driftRateMs, full.driftRateMs);
  TEST_ASSERT_EQUAL_UINT8(plain.presenceFlags, full.presenceFlags);
  TEST_ASSERT_EQUAL_UINT8(n, full.count);
  TEST_ASSERT_EQUAL_MEMORY(plain.entries, full.entries,
                           n * lp::WISP_STATE_ENTRY_SIZE);
  TEST_ASSERT_NOT_NULL(full.w);
  TEST_ASSERT_EQUAL_MEMORY(plain.w, full.w, n * lp::WISP_STATE_W_SIZE);

  lp::ParsedWispState legacy{};
  TEST_ASSERT_TRUE(legacyParseWispState(f.full, f.fullLen, legacy));
  TEST_ASSERT_EQUAL_MEMORY(plain.entries, legacy.entries,
                           n * lp::WISP_STATE_ENTRY_SIZE);
}

void test_state_trailer_records_round_trip() {
  // 30 records span two DRIFT_FADE TLVs; entries 30..39 have none.
  constexpr uint8_t kCount = 40;
  constexpr size_t kRecords = 30;
  static TrailerFrame f;
  buildTrailerFrame(f, kCount, kRecords, false);
  TEST_ASSERT_EQUAL_UINT32(
      f.plainLen + lp::wispStateDriftFadeTrailerSize(kRecords, false), f.fullLen);
  lp::ParsedWispState ws{};
  TEST_ASSERT_TRUE(lp::parseWispState(f.full, f.fullLen, ws));
  TEST_ASSERT_NOT_NULL(ws.trailer);
  uint8_t want[kRecords * lp::WISP_STATE_DRIFT_FADE_RECORD_SIZE];
  fillRecords(want, kRecords, 0);
  for (uint8_t e = 0; e < kCount; ++e) {
    lp::WispDriftFade got{};
    const auto k = lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, e, got);
    if (e >= kRecords) {
      TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::None, k);
      continue;
    }
    TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::Record, k);
    const uint8_t* r = want + e * lp::WISP_STATE_DRIFT_FADE_RECORD_SIZE;
    TEST_ASSERT_EQUAL_MEMORY(r + 1, got.base, 4);
    TEST_ASSERT_EQUAL_MEMORY(r + 5, got.shade, 4);
    const uint32_t ds = r[9] | (static_cast<uint32_t>(r[10]) << 8);
    TEST_ASSERT_EQUAL_UINT32(ds * lp::WISP_STATE_DRIFT_FADE_LEFT_UNIT_MS, got.leftMs);
  }
}

void test_state_partial_trailer_marks_missing_records() {
  static TrailerFrame f;
  buildTrailerFrame(f, 20, 5, true);
  lp::ParsedWispState ws{};
  TEST_ASSERT_TRUE(lp::parseWispState(f.full, f.fullLen, ws));
  lp::WispDriftFade got{};
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::Record,
                    lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, 4, got));
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::Partial,
                    lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, 5, got));
  // No room for a record still carries the mark.
  buildTrailerFrame(f, 20, 0, true);
  TEST_ASSERT_TRUE(lp::parseWispState(f.full, f.fullLen, ws));
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::Partial,
                    lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, 0, got));
}

void test_state_malformed_trailer_reads_none() {
  static TrailerFrame f;
  buildTrailerFrame(f, 10, 5, true);
  lp::ParsedWispState ws{};
  lp::WispDriftFade got{};
  // Truncated: the last TLV's len byte is past the frame. The trailer sits in
  // an exact-size buffer so a read past it trips ASan.
  TEST_ASSERT_TRUE(lp::parseWispState(f.full, f.fullLen - 1, ws));
  TEST_ASSERT_EQUAL_UINT8(10, ws.count);
  TEST_ASSERT_NOT_NULL(ws.w);
  const std::vector<uint8_t> exact(ws.trailer, ws.trailer + ws.trailerLen);
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::None,
                    lp::lookupWispDriftFade(exact.data(), exact.size(), 0, got));
  // A TLV value runs past the frame.
  buildTrailerFrame(f, 10, 5, false);
  TEST_ASSERT_TRUE(lp::parseWispState(f.full, f.fullLen - 1, ws));
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::None,
                    lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, 0, got));
  // tlv_count claims more TLVs than the frame holds.
  buildTrailerFrame(f, 10, 5, true);
  f.full[f.plainLen] += 1;
  TEST_ASSERT_TRUE(lp::parseWispState(f.full, f.fullLen, ws));
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::None,
                    lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, 0, got));
  // A DRIFT_FADE value that is not whole records.
  buildTrailerFrame(f, 10, 5, false);
  f.full[f.plainLen + 2] -= 1;
  TEST_ASSERT_TRUE(lp::parseWispState(f.full, f.fullLen - 1, ws));
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::None,
                    lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, 0, got));
}

void test_state_unknown_trailer_tlv_skipped() {
  static TrailerFrame f;
  buildTrailerFrame(f, 10, 3, false);
  // Prepend an unknown 2-byte TLV ahead of the records.
  uint8_t* t = &f.full[f.plainLen];
  const size_t tail = f.fullLen - f.plainLen - 1;
  std::memmove(t + 1 + 4, t + 1, tail);
  t[0] += 1;
  t[1] = 0x7E;
  t[2] = 2;
  t[3] = 0xAB;
  t[4] = 0xCD;
  lp::ParsedWispState ws{};
  TEST_ASSERT_TRUE(lp::parseWispState(f.full, f.fullLen + 4, ws));
  lp::WispDriftFade got{};
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::Record,
                    lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, 2, got));
}

void test_state_trailer_needs_w_plane() {
  uint8_t entries[3 * lp::WISP_STATE_ENTRY_SIZE];
  fillPattern(entries, sizeof(entries), 1, 0);
  uint8_t recs[lp::WISP_STATE_DRIFT_FADE_RECORD_SIZE];
  fillRecords(recs, 1, 0);
  uint8_t buf[lp::ESPNOW_V2_FRAME_MAX];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 64, 8000, 0,
                                      entries, nullptr, 3);
  TEST_ASSERT_EQUAL_UINT32(n, lp::appendWispStateDriftFade(buf, sizeof(buf), n,
                                                           recs, 1, false));
  // Nor is a trailer appended past a frame's room.
  const size_t withW = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 64, 8000,
                                          0, entries, entries, 3);
  TEST_ASSERT_EQUAL_UINT32(withW, lp::appendWispStateDriftFade(
                                      buf, withW + 2, withW, recs, 1, false));
}

void test_state_fit_line() {
  bool partial = true;
  const size_t fit = lp::WISP_STATE_DRIFT_FADE_MAX_RECORDS;
  TEST_ASSERT_EQUAL_UINT32(fit, lp::wispStateDriftFadeRecordsToSend(fit, fit, partial));
  TEST_ASSERT_FALSE(partial);
  TEST_ASSERT_TRUE(lp::wispStateFrameSize(fit) +
                       lp::wispStateDriftFadeTrailerSize(fit, false) <=
                   lp::ESPNOW_V2_FRAME_MAX);
  const size_t over = lp::wispStateDriftFadeRecordsToSend(fit + 1, fit + 1, partial);
  TEST_ASSERT_TRUE(partial);
  TEST_ASSERT_TRUE(over < fit + 1);
  TEST_ASSERT_TRUE(lp::wispStateFrameSize(fit + 1) +
                       lp::wispStateDriftFadeTrailerSize(fit + 1, false) >
                   lp::ESPNOW_V2_FRAME_MAX);
  // Every entry count up to the cap, every fading count: the frame fits.
  for (size_t n = 0; n <= lp::WISP_STATE_MAX_ENTRIES; ++n) {
    for (size_t fading = 0; fading <= n; ++fading) {
      const size_t k = lp::wispStateDriftFadeRecordsToSend(n, fading, partial);
      TEST_ASSERT_TRUE(k <= fading);
      TEST_ASSERT_TRUE(k <= lp::WISP_STATE_DRIFT_FADE_MAX_RECORDS);
      TEST_ASSERT_EQUAL(k < fading, partial);
      if (k == 0 && !partial) continue;
      TEST_ASSERT_TRUE(lp::wispStateFrameSize(n) +
                           lp::wispStateDriftFadeTrailerSize(k, partial) <=
                       lp::ESPNOW_V2_FRAME_MAX);
      // One more record would not have fit.
      if (partial) {
        TEST_ASSERT_TRUE(lp::wispStateFrameSize(n) +
                             lp::wispStateDriftFadeTrailerSize(k + 1, true) >
                         lp::ESPNOW_V2_FRAME_MAX);
      }
    }
  }
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_frame_caps);
  RUN_TEST(test_claim_fullset_round_trip);
  RUN_TEST(test_state_frame_caps);
  RUN_TEST(test_state_fullset_round_trip);
  RUN_TEST(test_state_zero_entries_round_trip);
  RUN_TEST(test_state_globals_survive_independent_of_count);
  RUN_TEST(test_state_over_cap_rejected);
  RUN_TEST(test_state_truncated_rejected);
  RUN_TEST(test_state_find_own_entry_picks_base_shade);
  RUN_TEST(test_state_find_own_entry_absent_returns_null);
  RUN_TEST(test_state_legacy_parser_reads_w_frame);
  RUN_TEST(test_state_w_plane_round_trip);
  RUN_TEST(test_state_without_w_plane_parses_with_null_w);
  RUN_TEST(test_state_parser_rejects_over_cap_count);
  RUN_TEST(test_state_132_parser_reads_trailer_frame_unchanged);
  RUN_TEST(test_state_trailer_records_round_trip);
  RUN_TEST(test_state_partial_trailer_marks_missing_records);
  RUN_TEST(test_state_malformed_trailer_reads_none);
  RUN_TEST(test_state_unknown_trailer_tlv_skipped);
  RUN_TEST(test_state_trailer_needs_w_plane);
  RUN_TEST(test_state_fit_line);
  return UNITY_END();
}
