// MSG_WISP_STATE post: decodeOwnWispDriftFade finds this lamp's record by its
// own MAC, and decode-then-copy drains the same bytes as a reference fill that
// looks up every entry's record inside the copy, over full, partial, absent,
// malformed and trailer-less frames posted back to back.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

struct TestMux {};
#define portENTER_CRITICAL(x) do { (void)(x); } while (0)
#define portEXIT_CRITICAL(x)  do { (void)(x); } while (0)
using portMUX_TYPE = TestMux;

#include "core/pending_typed_slot.hpp"
#include "components/network/mesh/pending_slots.cpp"

namespace lp = lamp_protocol;
using lamp::OwnWispDriftFade;
using lamp::PendingTypedSlot;
using lamp::PendingWispState;

void setUp(void) {}
void tearDown(void) {}

namespace {
portMUX_TYPE gMux{};

constexpr uint8_t kCount = 5;
constexpr uint8_t kMineIdx = 3;
const uint8_t kSrc[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
// Same OUI and first five bytes as kMine, as fleet MACs are.
const uint8_t kMine[6]  = {0x24, 0x6F, 0x28, 0x51, 0x9C, 0x3E};
const uint8_t kWrong[6] = {0x24, 0x6F, 0x28, 0x51, 0x9C, 0x3F};

enum class Form {
  Full,              // records for 0, mine, 4
  FullOther,         // records for 0 and 4 only
  PartialMine,       // records for 1 and mine, marked partial
  PartialOther,      // a record for 1, marked partial
  OwnAbsent,         // a record at mine's index, mine not in the entries
  FoundThenMalformed,// records for mine and 0, then a TLV that overruns the frame
  NoTrailer,         // W plane, no trailer
  NoW,               // entries only
};
constexpr Form kForms[] = {Form::Full,         Form::FullOther,
                           Form::PartialMine,  Form::PartialOther,
                           Form::OwnAbsent,    Form::FoundThenMalformed,
                           Form::NoTrailer,    Form::NoW};

struct Frame {
  uint8_t buf[lp::ESPNOW_V2_FRAME_MAX];
  size_t len;
  lp::ParsedWispState ws;
};

void putRecord(uint8_t* r, uint8_t idx, uint8_t seed) {
  r[0] = idx;
  for (uint8_t i = 1; i < 9; ++i) r[i] = static_cast<uint8_t>(seed * 17 + i * 29);
  r[9] = static_cast<uint8_t>(seed * 3 + 1);
  r[10] = static_cast<uint8_t>(seed);
}

// `seed` varies every colour and record so a stale byte from an earlier post
// differs from a fresh one.
void build(Frame& f, Form form, uint8_t seed) {
  uint8_t entries[kCount * lp::WISP_STATE_ENTRY_SIZE];
  uint8_t w[kCount * lp::WISP_STATE_W_SIZE];
  for (uint8_t i = 0; i < kCount; ++i) {
    uint8_t* e = entries + i * lp::WISP_STATE_ENTRY_SIZE;
    std::memcpy(e, kMine, 6);
    e[5] = static_cast<uint8_t>(0x40 + i);
    for (uint8_t j = 6; j < 12; ++j) e[j] = static_cast<uint8_t>(seed + i * 11 + j);
    w[i * 2] = static_cast<uint8_t>(seed + i);
    w[i * 2 + 1] = static_cast<uint8_t>(seed + i + 100);
  }
  if (form != Form::OwnAbsent) {
    std::memcpy(entries + kMineIdx * lp::WISP_STATE_ENTRY_SIZE, kMine, 6);
  }
  f.len = lp::buildWispState(f.buf, sizeof(f.buf), seed, kSrc, 100, 60000, 0,
                             entries, form == Form::NoW ? nullptr : w, kCount);
  TEST_ASSERT_NOT_EQUAL(0, f.len);

  uint8_t recs[3 * lp::WISP_STATE_DRIFT_FADE_RECORD_SIZE];
  auto rec = [&](size_t at, uint8_t idx) {
    putRecord(recs + at * lp::WISP_STATE_DRIFT_FADE_RECORD_SIZE, idx,
              static_cast<uint8_t>(seed + idx));
  };
  size_t n = 0;
  bool partial = false;
  switch (form) {
    case Form::Full: rec(0, 0); rec(1, kMineIdx); rec(2, 4); n = 3; break;
    case Form::FullOther: rec(0, 0); rec(1, 4); n = 2; break;
    case Form::PartialMine: rec(0, 1); rec(1, kMineIdx); n = 2; partial = true; break;
    case Form::PartialOther: rec(0, 1); n = 1; partial = true; break;
    case Form::OwnAbsent: rec(0, kMineIdx); n = 1; break;
    case Form::FoundThenMalformed: rec(0, kMineIdx); rec(1, 0); n = 2; break;
    case Form::NoTrailer:
    case Form::NoW: break;
  }
  if (n || partial) {
    const size_t before = f.len;
    f.len = lp::appendWispStateDriftFade(f.buf, sizeof(f.buf), f.len, recs, n,
                                         partial);
    TEST_ASSERT_GREATER_THAN(before, f.len);
  }
  if (form == Form::FoundThenMalformed) {
    const size_t trailerAt = lp::wispStateFrameSize(kCount);
    ++f.buf[trailerAt];
    f.buf[f.len++] = lp::WISP_STATE_TLV_DRIFT_FADE;
    f.buf[f.len++] = 200;
  }
  TEST_ASSERT_TRUE(lp::parseWispState(f.buf, f.len, f.ws));
}

// Every lookup done inside the copy, straight into the slot. The
// reference every drained byte is held to.
void referenceFill(PendingWispState& slot, const lp::ParsedWispState& ws,
                   const uint8_t selfMac[6]) {
  std::memcpy(slot.sourceMac, ws.sourceMac, 6);
  slot.count = ws.count;
  const size_t n = ws.entries ? ws.count : 0;
  if (n) std::memcpy(slot.entries, ws.entries, n * lp::WISP_STATE_ENTRY_SIZE);
  if (n && ws.w) {
    std::memcpy(slot.w, ws.w, n * lp::WISP_STATE_W_SIZE);
  } else {
    std::memset(slot.w, 0, n * lp::WISP_STATE_W_SIZE);
  }
  slot.fade = lp::WispDriftFadeLookup::None;
  const uint8_t* own =
      selfMac ? lp::findWispStateEntry(ws.entries, ws.count, selfMac) : nullptr;
  if (own) {
    const auto idx =
        static_cast<uint8_t>((own - ws.entries) / lp::WISP_STATE_ENTRY_SIZE);
    slot.fade = lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, idx,
                                        slot.fadeRecord);
    slot.ownIndex = idx;
  } else {
    slot.ownIndex = PendingWispState::kNoOwnEntry;
  }
  // No entry has index 0xFE, so its lookup reads only the PARTIAL mark.
  lp::WispDriftFade none{};
  slot.fadePartial = lp::lookupWispDriftFade(ws.trailer, ws.trailerLen, 0xFE,
                                             none) ==
                     lp::WispDriftFadeLookup::Partial;
  std::memset(slot.targeted, 0, sizeof(slot.targeted));
  for (size_t i = 0; i < n && ws.w; ++i) {
    if (i == slot.ownIndex) continue;
    lp::WispDriftFade rec{};
    if (lp::lookupWispDriftFade(ws.trailer, ws.trailerLen,
                                static_cast<uint8_t>(i), rec) !=
        lp::WispDriftFadeLookup::Record) {
      continue;
    }
    uint8_t* e = slot.entries + i * lp::WISP_STATE_ENTRY_SIZE;
    std::memcpy(e + 6, rec.base, 3);
    std::memcpy(e + 9, rec.shade, 3);
    slot.w[i * lp::WISP_STATE_W_SIZE] = rec.base[3];
    slot.w[i * lp::WISP_STATE_W_SIZE + 1] = rec.shade[3];
    slot.targeted[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
  }
}

// What postPendingWispState does: decode on the caller, copy under the lock.
void decodeThenPost(PendingTypedSlot<PendingWispState>& slot,
                    const lp::ParsedWispState& ws, const uint8_t selfMac[6]) {
  const OwnWispDriftFade own = lamp::decodeOwnWispDriftFade(ws, selfMac);
  slot.postWith(gMux, [&ws, &own](PendingWispState& s) {
    lamp::fillPendingWispState(s, ws, own);
  });
}

void referencePost(PendingTypedSlot<PendingWispState>& slot,
                   const lp::ParsedWispState& ws, const uint8_t selfMac[6]) {
  slot.postWith(gMux, [&ws, selfMac](PendingWispState& s) {
    referenceFill(s, ws, selfMac);
  });
}

void assertSameDrain(const PendingWispState& want, const PendingWispState& got,
                     const char* where) {
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want.sourceMac, got.sourceMac, 6, where);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.count, got.count, where);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want.entries, got.entries,
                                   sizeof(want.entries), where);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want.w, got.w, sizeof(want.w), where);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(want.fade),
                                  static_cast<uint8_t>(got.fade), where);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want.fadeRecord.base, got.fadeRecord.base,
                                   4, where);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want.fadeRecord.shade, got.fadeRecord.shade,
                                   4, where);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.fadeRecord.leftMs,
                                   got.fadeRecord.leftMs, where);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.ownIndex, got.ownIndex, where);
  TEST_ASSERT_EQUAL_MESSAGE(want.fadePartial, got.fadePartial, where);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want.targeted, got.targeted,
                                   sizeof(want.targeted), where);
}

const uint8_t* const kMacs[] = {kMine, nullptr, kWrong};

static Frame gFrame;
static PendingTypedSlot<PendingWispState> gRefSlot;
static PendingTypedSlot<PendingWispState> gNewSlot;
static PendingWispState gRefOut;
static PendingWispState gNewOut;

// Posts every form under every MAC, forms in reverse when `reverse`, draining
// after each post when `drainEach`, else only after each form's three posts.
void runSequence(bool reverse, bool drainEach) {
  gRefSlot = {};
  gNewSlot = {};
  const size_t forms = sizeof(kForms) / sizeof(kForms[0]);
  uint8_t seed = reverse ? 90 : 10;
  char where[48];
  for (size_t k = 0; k < forms; ++k) {
    const Form form = kForms[reverse ? forms - 1 - k : k];
    for (size_t m = 0; m < 3; ++m) {
      build(gFrame, form, seed++);
      referencePost(gRefSlot, gFrame.ws, kMacs[m]);
      decodeThenPost(gNewSlot, gFrame.ws, kMacs[m]);
      if (!drainEach && m < 2) continue;
      std::snprintf(where, sizeof(where), "form %u mac %u",
                    static_cast<unsigned>(form), static_cast<unsigned>(m));
      TEST_ASSERT_TRUE(gRefSlot.drain(gMux, gRefOut));
      TEST_ASSERT_TRUE(gNewSlot.drain(gMux, gNewOut));
      assertSameDrain(gRefOut, gNewOut, where);
    }
  }
}
}  // namespace

void test_decode_reads_own_record_at_its_index() {
  build(gFrame, Form::Full, 7);
  const OwnWispDriftFade own = lamp::decodeOwnWispDriftFade(gFrame.ws, kMine);
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::Record, own.fade);
  TEST_ASSERT_TRUE(own.recordWritten);
  uint8_t want[lp::WISP_STATE_DRIFT_FADE_RECORD_SIZE];
  putRecord(want, kMineIdx, static_cast<uint8_t>(7 + kMineIdx));
  TEST_ASSERT_EQUAL_MEMORY(want + 1, own.record.base, 4);
  TEST_ASSERT_EQUAL_MEMORY(want + 5, own.record.shade, 4);
  const uint32_t ds = static_cast<uint32_t>(want[9]) |
                      (static_cast<uint32_t>(want[10]) << 8);
  TEST_ASSERT_EQUAL_UINT32(ds * lp::WISP_STATE_DRIFT_FADE_LEFT_UNIT_MS,
                           own.record.leftMs);
}

void test_decode_without_own_mac_reads_none() {
  build(gFrame, Form::Full, 7);
  for (const uint8_t* mac : {static_cast<const uint8_t*>(nullptr), kWrong}) {
    const OwnWispDriftFade own = lamp::decodeOwnWispDriftFade(gFrame.ws, mac);
    TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::None, own.fade);
    TEST_ASSERT_FALSE(own.recordWritten);
  }
}

void test_decode_reports_a_record_a_malformed_trailer_voids() {
  build(gFrame, Form::FoundThenMalformed, 7);
  const OwnWispDriftFade own = lamp::decodeOwnWispDriftFade(gFrame.ws, kMine);
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::None, own.fade);
  TEST_ASSERT_TRUE(own.recordWritten);
}

// A reused flag still holding true reads false after a lookup that finds no
// record.
void test_lookup_clears_a_stale_written_flag() {
  build(gFrame, Form::Full, 7);
  lp::WispDriftFade rec{};
  bool written = true;
  TEST_ASSERT_EQUAL(lp::WispDriftFadeLookup::None,
                    lp::lookupWispDriftFade(gFrame.ws.trailer,
                                            gFrame.ws.trailerLen, 2, rec,
                                            &written));
  TEST_ASSERT_FALSE(written);
}

void test_decode_then_copy_drains_as_reference_each_post() {
  runSequence(false, true);
  runSequence(true, true);
}

void test_decode_then_copy_drains_as_reference_over_overwrites() {
  runSequence(false, false);
  runSequence(true, false);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_decode_reads_own_record_at_its_index);
  RUN_TEST(test_decode_without_own_mac_reads_none);
  RUN_TEST(test_decode_reports_a_record_a_malformed_trailer_voids);
  RUN_TEST(test_lookup_clears_a_stale_written_flag);
  RUN_TEST(test_decode_then_copy_drains_as_reference_each_post);
  RUN_TEST(test_decode_then_copy_drains_as_reference_over_overwrites);
  return UNITY_END();
}
