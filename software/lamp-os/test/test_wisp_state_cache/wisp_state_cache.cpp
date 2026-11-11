// LampRoster::cacheWispState, the drain's feed into the claims blob: a STATE
// slot's W plane and painted entries (black included) reach CHAR_WISP_CLAIMS.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/network/mesh/lamp_roster.cpp"
#include "components/network/mesh/pending_slots.cpp"
#include "components/network/mesh/wisp_cache.cpp"
#include "components/network/mesh/wisp_fleet_cache.cpp"
#include "components/network/mesh/wisp_status_merge.cpp"

namespace lp = lamp_protocol;
using lamp::LampRoster;
using lamp::PendingWispState;
using lamp::WispFleetCache;

void setUp(void) {}
void tearDown(void) {}

namespace {
const uint8_t kSrc[6]   = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
const uint8_t kWarm[6]  = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
const uint8_t kBlack[6] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25};

PendingWispState gSlot;

const uint8_t kWarmThenBlackW[4] = {0xC8, 0x37, 0, 0};

// kBlack's RGB is always zero; its W comes from `w`.
void fill(bool withW, const uint8_t w[4] = kWarmThenBlackW) {
  uint8_t entries[2 * lp::WISP_STATE_ENTRY_SIZE] = {};
  std::memcpy(entries, kWarm, 6);
  std::memcpy(entries + 12, kBlack, 6);
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 100, 60000, 0,
                                      entries, withW ? w : nullptr, 2);
  lp::ParsedWispState ws;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, ws));
  lamp::fillPendingWispState(gSlot, ws, nullptr);
}
}  // namespace

void test_state_slot_reaches_claims_blob() {
  LampRoster roster;
  // kBlack first paints warm white, so black must overwrite both W bytes.
  const uint8_t warmBlackW[4] = {0xC8, 0x37, 0x51, 0x52};
  fill(true, warmBlackW);
  roster.cacheWispState(gSlot, 500);
  fill(true);
  roster.cacheWispState(gSlot, 1000);

  constexpr size_t kE = WispFleetCache::kBlobEntrySize;
  uint8_t blob[1 + 2 * kE];
  TEST_ASSERT_EQUAL_size_t(sizeof(blob),
                           roster.buildWispClaimsBlob(blob, sizeof(blob), 1000));
  TEST_ASSERT_EQUAL_UINT8(2, blob[0]);
  const size_t ext = 1 + 2 * 12;
  for (size_t i = 0; i < 2; ++i) {
    const uint8_t* mac = blob + 1 + i * 6;
    const uint8_t* e = blob + ext + i * 3;
    TEST_ASSERT_EQUAL_UINT8(1, e[0]);
    if (std::memcmp(mac, kWarm, 6) == 0) {
      TEST_ASSERT_EQUAL_UINT8(0xC8, e[1]);
      TEST_ASSERT_EQUAL_UINT8(0x37, e[2]);
    } else {
      TEST_ASSERT_EQUAL_MEMORY(kBlack, mac, 6);
      TEST_ASSERT_EQUAL_UINT8(0, e[1]);
      TEST_ASSERT_EQUAL_UINT8(0, e[2]);
    }
  }
}

void test_state_slot_without_w_caches_w_zero() {
  LampRoster roster;
  fill(true);
  roster.cacheWispState(gSlot, 1000);
  fill(false);
  roster.cacheWispState(gSlot, 2000);

  uint8_t blob[1 + 2 * WispFleetCache::kBlobEntrySize];
  TEST_ASSERT_EQUAL_size_t(sizeof(blob),
                           roster.buildWispClaimsBlob(blob, sizeof(blob), 2000));
  TEST_ASSERT_EQUAL_UINT8(2, blob[0]);
  const size_t ext = 1 + 2 * 12;
  for (size_t i = 0; i < 2; ++i) {
    TEST_ASSERT_EQUAL_UINT8(1, blob[ext + i * 3]);
    TEST_ASSERT_EQUAL_UINT8(0, blob[ext + i * 3 + 1]);
    TEST_ASSERT_EQUAL_UINT8(0, blob[ext + i * 3 + 2]);
  }
}

void test_state_slot_past_capacity_keeps_frame_head() {
  constexpr size_t kOver = WispFleetCache::kCapacity + 3;
  static_assert(kOver <= lp::WISP_STATE_MAX_ENTRIES,
                "fixture must fit one STATE frame");
  uint8_t entries[kOver * lp::WISP_STATE_ENTRY_SIZE] = {};
  for (size_t i = 0; i < kOver; ++i) {
    uint8_t* e = entries + i * lp::WISP_STATE_ENTRY_SIZE;
    e[0] = 0x30;
    e[5] = static_cast<uint8_t>(i);
    e[6] = static_cast<uint8_t>(i + 1);
  }
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 100, 60000, 0,
                                      entries, nullptr,
                                      static_cast<uint8_t>(kOver));
  lp::ParsedWispState ws;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, ws));
  lamp::fillPendingWispState(gSlot, ws, nullptr);
  TEST_ASSERT_EQUAL_UINT8(kOver, gSlot.count);

  LampRoster roster;
  roster.cacheWispState(gSlot, 1000);

  constexpr size_t kCap = WispFleetCache::kCapacity;
  static uint8_t blob[1 + kCap * WispFleetCache::kBlobEntrySize];
  roster.buildWispClaimsBlob(blob, sizeof(blob), 1000);
  TEST_ASSERT_EQUAL_UINT8(kCap, blob[0]);
  bool seen[kOver] = {};
  for (size_t i = 0; i < kCap; ++i) {
    const uint8_t idx = blob[1 + i * 6 + 5];
    TEST_ASSERT_LESS_THAN_size_t(kOver, idx);
    seen[idx] = true;
  }
  for (size_t i = 0; i < kCap; ++i) TEST_ASSERT_TRUE_MESSAGE(seen[i], "head entry evicted");
  for (size_t i = kCap; i < kOver; ++i) TEST_ASSERT_FALSE(seen[i]);
}

namespace {
// Retires once every 2^28 ms (about three days) from `fromMs` to just short of
// one full wrap later.
void retireThroughAWrap(LampRoster& roster, uint32_t fromMs) {
  for (uint64_t t = fromMs; t < fromMs + (1ull << 32); t += 1ull << 28) {
    roster.retireStaleWisp(static_cast<uint32_t>(t));
  }
}

void hello(LampRoster& roster) {
  roster.cacheWispHello(kSrc, 1, 0, "pal00000", "beta0000", 0);
}
}  // namespace

// A wisp heard once, then silent for a whole wrap: neither its display slot
// nor its control target may read fresh again.
void test_silent_wisp_slot_stays_stale_a_wrap_later() {
  LampRoster roster;
  const uint32_t heardMs = 1000;
  set_mock_millis(heardMs);
  hello(roster);
  retireThroughAWrap(roster, heardMs);
  uint8_t mac[6];
  TEST_ASSERT_FALSE(roster.isClaimingWisp(kSrc, heardMs + 5000));
  TEST_ASSERT_FALSE(roster.copyDisplayWispMac(mac, heardMs + 5000));
}

// A STATE once, then silence: its fleet entries are gone a wrap later.
void test_silent_fleet_entries_stay_stale_a_wrap_later() {
  LampRoster roster;
  fill(true);
  roster.cacheWispState(gSlot, 1000);
  retireThroughAWrap(roster, 1000);
  uint8_t blob[1 + 2 * WispFleetCache::kBlobEntrySize];
  roster.buildWispClaimsBlob(blob, sizeof(blob), 1000 + 5000);
  TEST_ASSERT_EQUAL_UINT8(0, blob[0]);
}

// Away from the wrap retiring changes nothing: a fresh slot stays fresh, and
// a hello after an age-out makes it fresh again.
void test_retire_keeps_a_fresh_wisp_slot() {
  LampRoster roster;
  set_mock_millis(1000);
  hello(roster);
  fill(true);
  roster.cacheWispState(gSlot, 1000);
  roster.retireStaleWisp(1000 + 11000);
  uint8_t mac[6];
  TEST_ASSERT_TRUE(roster.isClaimingWisp(kSrc, 1000 + 11000));
  TEST_ASSERT_TRUE(roster.copyDisplayWispMac(mac, 1000 + 11000));
  uint8_t blob[1 + 2 * WispFleetCache::kBlobEntrySize];
  roster.buildWispClaimsBlob(blob, sizeof(blob), 1000 + 11000);
  TEST_ASSERT_EQUAL_UINT8(2, blob[0]);

  roster.retireStaleWisp(1000 + 70000);
  TEST_ASSERT_FALSE(roster.isClaimingWisp(kSrc, 1000 + 70000));
  set_mock_millis(1000 + 80000);
  hello(roster);
  TEST_ASSERT_TRUE(roster.isClaimingWisp(kSrc, 1000 + 80001));
  TEST_ASSERT_TRUE(roster.copyDisplayWispMac(mac, 1000 + 80001));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_state_slot_reaches_claims_blob);
  RUN_TEST(test_state_slot_without_w_caches_w_zero);
  RUN_TEST(test_state_slot_past_capacity_keeps_frame_head);
  RUN_TEST(test_silent_wisp_slot_stays_stale_a_wrap_later);
  RUN_TEST(test_silent_fleet_entries_stay_stale_a_wrap_later);
  RUN_TEST(test_retire_keeps_a_fresh_wisp_slot);
  return UNITY_END();
}
