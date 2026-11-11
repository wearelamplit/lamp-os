// PendingTypedSlot::postWith and the fills the recv task posts through it.

#include <unity.h>

#include <cstdint>
#include <cstring>

namespace {
int gMuxDepth = 0;
}  // namespace

struct TestMux {};
#define portENTER_CRITICAL(x) do { (void)(x); ++gMuxDepth; } while (0)
#define portEXIT_CRITICAL(x)  do { (void)(x); --gMuxDepth; } while (0)
using portMUX_TYPE = TestMux;

#include "core/pending_typed_slot.hpp"
#include "components/network/mesh/pending_slots.cpp"

namespace lp = lamp_protocol;
using lamp::PendingCommand;
using lamp::PendingTypedSlot;
using lamp::PendingWispClaim;

void setUp(void) { gMuxDepth = 0; }
void tearDown(void) {}

namespace {
portMUX_TYPE gMux{};

void fillMac(uint8_t* mac, uint8_t seed) {
  for (uint8_t i = 0; i < 6; ++i) mac[i] = static_cast<uint8_t>(seed * 7 + i * 13 + 1);
}

// count claim entries; MAC bytes depend on the index and rssi is nonzero, so a
// wrong stride shows in the slot.
void buildClaimEntries(uint8_t* buf, size_t count, uint8_t seed) {
  for (size_t i = 0; i < count; ++i) {
    uint8_t* e = buf + i * lp::WISP_CLAIM_ENTRY_SIZE;
    fillMac(e, static_cast<uint8_t>(seed + i));
    e[6] = static_cast<uint8_t>(0xC0 + i);
  }
}
}  // namespace

void test_postWith_fills_inside_critical_section() {
  PendingTypedSlot<PendingCommand> slot;
  int depthSeen = -1;
  slot.postWith(gMux, [&](PendingCommand& p) {
    depthSeen = gMuxDepth;
    p.payloadLen = 3;
  });
  TEST_ASSERT_EQUAL_INT(1, depthSeen);
  TEST_ASSERT_EQUAL_INT(0, gMuxDepth);
  TEST_ASSERT_TRUE(slot.valid);
  TEST_ASSERT_EQUAL_UINT16(3, slot.payload.payloadLen);
}

void test_postWith_newest_writer_wins() {
  PendingTypedSlot<PendingCommand> slot;
  slot.postWith(gMux, [](PendingCommand& p) { p.payloadLen = 5; });
  slot.postWith(gMux, [](PendingCommand& p) { p.payloadLen = 9; });
  static PendingCommand out;
  TEST_ASSERT_TRUE(slot.drain(gMux, out));
  TEST_ASSERT_EQUAL_UINT16(9, out.payloadLen);
  TEST_ASSERT_FALSE(slot.drain(gMux, out));
}

void test_claim_fill_copies_macs_only() {
  static uint8_t entries[lp::kMaxWispClaimEntries * lp::WISP_CLAIM_ENTRY_SIZE];
  buildClaimEntries(entries, 4, 3);
  lp::ParsedWispClaim wc{};
  fillMac(wc.sourceMac, 90);
  wc.count = 4;
  wc.entries = entries;

  static PendingWispClaim slot{};
  std::memset(slot.lampMacs, 0xEE, sizeof(slot.lampMacs));
  lamp::fillPendingWispClaim(slot, wc);
  TEST_ASSERT_EQUAL_MEMORY(wc.sourceMac, slot.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8(4, slot.count);
  TEST_ASSERT_EQUAL_HEX8(0xEE, slot.lampMacs[4][0]);
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_MEMORY(entries + i * lp::WISP_CLAIM_ENTRY_SIZE,
                             slot.lampMacs[i], 6);
  }
}

void test_claim_fill_clamps_count() {
  constexpr size_t kOver = lp::kMaxWispClaimEntries + 3;
  static uint8_t entries[kOver * lp::WISP_CLAIM_ENTRY_SIZE];
  buildClaimEntries(entries, kOver, 1);
  lp::ParsedWispClaim wc{};
  wc.count = static_cast<uint8_t>(kOver);
  wc.entries = entries;

  static PendingWispClaim slot{};
  lamp::fillPendingWispClaim(slot, wc);
  TEST_ASSERT_EQUAL_UINT8(lp::kMaxWispClaimEntries, slot.count);
  const size_t last = lp::kMaxWispClaimEntries - 1;
  TEST_ASSERT_EQUAL_MEMORY(entries + last * lp::WISP_CLAIM_ENTRY_SIZE,
                           slot.lampMacs[last], 6);
}

void test_command_fill_copies_payload() {
  uint8_t payload[200];
  for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i * 31 + 7);
  lp::ParsedCommand p{};
  fillMac(p.sourceMac, 40);
  p.payload = payload;
  p.payloadLen = 137;

  static PendingCommand slot{};
  lamp::fillPendingCommand(slot, p);
  TEST_ASSERT_EQUAL_MEMORY(p.sourceMac, slot.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT16(137, slot.payloadLen);
  TEST_ASSERT_EQUAL_MEMORY(payload, slot.payload, 137);
}

// A fill writes only the fields its frame carries, so the bytes past `count`
// must reach it as the previous post left them.
void test_postWith_hands_fill_the_previous_payload() {
  PendingTypedSlot<PendingWispClaim> slot;
  slot.postWith(gMux, [](PendingWispClaim& p) {
    p.count = 9;
    p.lampMacs[8][5] = 0x5A;
  });
  bool sawPrevious = false;
  slot.postWith(gMux, [&](PendingWispClaim& p) {
    sawPrevious = p.count == 9 && p.lampMacs[8][5] == 0x5A;
    p.count = 2;
  });
  TEST_ASSERT_TRUE(sawPrevious);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_postWith_fills_inside_critical_section);
  RUN_TEST(test_postWith_newest_writer_wins);
  RUN_TEST(test_claim_fill_copies_macs_only);
  RUN_TEST(test_claim_fill_clamps_count);
  RUN_TEST(test_command_fill_copies_payload);
  RUN_TEST(test_postWith_hands_fill_the_previous_payload);
  return UNITY_END();
}
