// PendingTypedSlot::postWith and the fills the recv task posts through it.
// Pins that the fill runs inside the critical section, and that filling the
// slot in place leaves the loop draining the same bytes a staged copy did.

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
using lamp::PendingWispState;

void setUp(void) { gMuxDepth = 0; }
void tearDown(void) {}

namespace {
portMUX_TYPE gMux{};

void fillMac(uint8_t* mac, uint8_t seed) {
  for (uint8_t i = 0; i < 6; ++i) mac[i] = static_cast<uint8_t>(seed * 7 + i * 13 + 1);
}

// Old path: the recv task filled a staging T, then post() copied it whole.
// New path: postWith fills the slot's payload in place. Both drains must
// match byte for byte, stale tail past `count` included.
template <typename T, typename Fill>
void postBothAndCompare(T& staging, PendingTypedSlot<T>& viaCopy,
                        PendingTypedSlot<T>& inPlace, Fill fill) {
  fill(staging);
  viaCopy.post(gMux, staging);
  inPlace.postWith(gMux, fill);

  static T a, b;
  TEST_ASSERT_TRUE(viaCopy.drain(gMux, a));
  TEST_ASSERT_TRUE(inPlace.drain(gMux, b));
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(T));
}

// count claim entries; MAC bytes depend on the index and rssi is nonzero, so a
// wrong stride or a copied rssi byte shows in the slot.
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
  lamp::fillPendingWispClaim(slot, wc);
  TEST_ASSERT_EQUAL_MEMORY(wc.sourceMac, slot.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8(4, slot.count);
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

void test_claim_in_place_matches_staged_copy() {
  static PendingWispClaim staging{};
  static PendingTypedSlot<PendingWispClaim> viaCopy, inPlace;
  static uint8_t entries[lp::kMaxWispClaimEntries * lp::WISP_CLAIM_ENTRY_SIZE];
  const uint8_t counts[] = {9, 2, 0, 5};
  for (uint8_t round = 0; round < sizeof(counts); ++round) {
    buildClaimEntries(entries, counts[round], static_cast<uint8_t>(round * 20));
    lp::ParsedWispClaim wc{};
    fillMac(wc.sourceMac, round);
    wc.count = counts[round];
    wc.entries = entries;
    postBothAndCompare(staging, viaCopy, inPlace,
                       [&wc](PendingWispClaim& s) { lamp::fillPendingWispClaim(s, wc); });
  }
}

void test_command_in_place_matches_staged_copy() {
  static PendingCommand staging{};
  static PendingTypedSlot<PendingCommand> viaCopy, inPlace;
  static uint8_t payload[lp::COMMAND_MAX_PAYLOAD];
  const uint16_t lens[] = {300, 11, static_cast<uint16_t>(lp::COMMAND_MAX_PAYLOAD), 64};
  for (uint8_t round = 0; round < 4; ++round) {
    for (size_t i = 0; i < lens[round]; ++i) payload[i] = static_cast<uint8_t>(i + round * 50 + 3);
    lp::ParsedCommand p{};
    fillMac(p.sourceMac, static_cast<uint8_t>(round + 10));
    p.payload = payload;
    p.payloadLen = lens[round];
    postBothAndCompare(staging, viaCopy, inPlace,
                       [&p](PendingCommand& s) { lamp::fillPendingCommand(s, p); });
  }
}

void test_wisp_state_in_place_matches_staged_copy() {
  static PendingWispState staging{};
  static PendingTypedSlot<PendingWispState> viaCopy, inPlace;
  static uint8_t entries[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_ENTRY_SIZE];
  static uint8_t w[lp::WISP_STATE_MAX_ENTRIES * lp::WISP_STATE_W_SIZE];
  const uint8_t counts[] = {40, 6, 17};
  const bool withW[] = {true, true, false};
  for (uint8_t round = 0; round < 3; ++round) {
    for (size_t i = 0; i < sizeof(entries); ++i) entries[i] = static_cast<uint8_t>(i * 5 + round);
    for (size_t i = 0; i < sizeof(w); ++i) w[i] = static_cast<uint8_t>(i * 3 + round + 1);
    lp::ParsedWispState ws{};
    fillMac(ws.sourceMac, static_cast<uint8_t>(round + 60));
    ws.count = counts[round];
    ws.entries = entries;
    ws.w = withW[round] ? w : nullptr;
    postBothAndCompare(staging, viaCopy, inPlace,
                       [&ws](PendingWispState& s) { lamp::fillPendingWispState(s, ws); });
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_postWith_fills_inside_critical_section);
  RUN_TEST(test_postWith_newest_writer_wins);
  RUN_TEST(test_claim_fill_copies_macs_only);
  RUN_TEST(test_claim_fill_clamps_count);
  RUN_TEST(test_command_fill_copies_payload);
  RUN_TEST(test_claim_in_place_matches_staged_copy);
  RUN_TEST(test_command_in_place_matches_staged_copy);
  RUN_TEST(test_wisp_state_in_place_matches_staged_copy);
  return UNITY_END();
}
