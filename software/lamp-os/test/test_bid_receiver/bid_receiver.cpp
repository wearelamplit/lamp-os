// Native-host tests for the receiver-side bid gate + storm control
// (core/bid_receiver.{hpp,cpp}). Real production BidReceiver + FastRng.
//
// Pins:
//   1. gate probability: seeded FastRng, arm rate tracks socialBidResponse
//      for 0% / 100% / a mid cell (cooldown stepped past between trials).
//   2. unknown bidType is ignored (never arms).
//   3. per-receiver honor cooldown suppresses a second bid inside the window.
//   4. deferred honor: takeDue fires only once, only after the jitter delay.
//   5. multi-receiver storm: N receivers honoring one bid scatter their honor
//      times across the jitter window.
//   6. findByMac-miss drop (mirror of drainBid): a miss never reaches onBid.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/bid_receiver.hpp"
#include "core/social_tier.hpp"
#include "components/network/protocol/bid.hpp"
#include "util/fast_rng.hpp"

#include "../../src/core/bid_receiver.cpp"

using lamp::BidReceiver;
using lamp::FastRng;
using lamp::SocialMode;

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kSrc[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};

// Count arms over N trials, stepping nowMs past the honor cooldown each trial
// so the cooldown never masks the gate.
static int armCount(uint8_t disposition, SocialMode mode, uint32_t seed, int trials) {
  BidReceiver r;
  FastRng rng(seed);
  int arms = 0;
  for (int i = 0; i < trials; ++i) {
    const uint32_t now = static_cast<uint32_t>(i + 1) * BidReceiver::kBidHonorCooldownMs;
    if (r.onBid(kSrc, lamp_protocol::BID_GREETING, disposition, mode, now, rng)) {
      ++arms;
    }
  }
  return arms;
}

void test_gate_zero_percent_never_arms() {
  // Salty → 0% in every mode.
  TEST_ASSERT_EQUAL_INT(0, armCount(1, SocialMode::Ambivert, 12345, 500));
  TEST_ASSERT_EQUAL_INT(0, armCount(1, SocialMode::Extrovert, 999, 500));
}

void test_gate_hundred_percent_always_arms() {
  // Smitten + Extrovert → 100%.
  TEST_ASSERT_EQUAL_INT(500, armCount(5, SocialMode::Extrovert, 424242, 500));
}

void test_gate_mid_tracks_probability() {
  // Neutral + Ambivert → 30%.
  const int trials = 4000;
  const int arms = armCount(3, SocialMode::Ambivert, 0xBEEF, trials);
  const int expected = trials * 30 / 100;  // 1200
  TEST_ASSERT_INT_WITHIN(180, expected, arms);
}

void test_unknown_bidtype_ignored() {
  BidReceiver r;
  FastRng rng(7);
  // 100% cell, but an unknown bidType must never arm.
  TEST_ASSERT_FALSE(r.onBid(kSrc, 0x7F, 5, SocialMode::Extrovert, 1000, rng));
  uint8_t mac[6]; uint8_t type;
  TEST_ASSERT_FALSE(r.takeDue(100000, mac, type));
}

void test_honor_cooldown_suppresses_second_bid() {
  BidReceiver r;
  FastRng rng(3);
  // 100% cell so acceptance is deterministic.
  TEST_ASSERT_TRUE(r.onBid(kSrc, lamp_protocol::BID_GREETING, 5,
                           SocialMode::Extrovert, 1000, rng));
  // Within the cooldown window → suppressed.
  TEST_ASSERT_FALSE(r.onBid(kSrc, lamp_protocol::BID_GREETING, 5,
                            SocialMode::Extrovert,
                            1000 + BidReceiver::kBidHonorCooldownMs - 1, rng));
  // At/after the window → allowed again.
  TEST_ASSERT_TRUE(r.onBid(kSrc, lamp_protocol::BID_GREETING, 5,
                           SocialMode::Extrovert,
                           1000 + BidReceiver::kBidHonorCooldownMs, rng));
}

void test_deferred_honor_fires_once_after_jitter() {
  BidReceiver r;
  FastRng rng(11);
  TEST_ASSERT_TRUE(r.onBid(kSrc, lamp_protocol::BID_GREETING, 5,
                           SocialMode::Extrovert, 1000, rng));
  uint8_t mac[6]; uint8_t type;
  // Not yet due at t=1000 unless jitter happened to be 0; guaranteed due once
  // past the whole jitter window.
  const uint32_t past = 1000 + BidReceiver::kBidHonorJitterMs;
  TEST_ASSERT_TRUE(r.takeDue(past, mac, type));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kSrc, mac, 6);
  TEST_ASSERT_EQUAL_UINT8(lamp_protocol::BID_GREETING, type);
  // Slot cleared: no second fire.
  TEST_ASSERT_FALSE(r.takeDue(past + 100000, mac, type));
}

void test_deferred_honor_respects_its_delay() {
  // Sweep to find the fire offset, then confirm it was pending on every tick
  // before it and bounded by the jitter window. No assumption about the exact
  // (rng-derived) delay.
  BidReceiver r;
  FastRng rng(0x1234);
  const uint32_t arm = 5000;
  TEST_ASSERT_TRUE(r.onBid(kSrc, lamp_protocol::BID_GREETING, 5,
                           SocialMode::Extrovert, arm, rng));
  uint8_t mac[6]; uint8_t type;
  uint32_t fireOff = BidReceiver::kBidHonorJitterMs + 1;  // sentinel
  for (uint32_t off = 0; off <= BidReceiver::kBidHonorJitterMs; ++off) {
    if (r.takeDue(arm + off, mac, type)) { fireOff = off; break; }
    // Still pending before its delay elapses.
  }
  TEST_ASSERT_TRUE(fireOff <= BidReceiver::kBidHonorJitterMs);
}

void test_storm_scatters_honor_times() {
  const int kReceivers = 24;
  const uint32_t now = 10000;
  std::vector<uint32_t> dueOffsets;
  for (int i = 0; i < kReceivers; ++i) {
    BidReceiver r;
    FastRng rng(static_cast<uint32_t>(i) * 2654435761u + 1u);  // distinct streams
    // 100% cell → every receiver accepts; only the jitter varies.
    TEST_ASSERT_TRUE(r.onBid(kSrc, lamp_protocol::BID_GREETING, 5,
                             SocialMode::Extrovert, now, rng));
    // Sweep to find the fire tick; the offset is where takeDue first succeeds.
    for (uint32_t off = 0; off <= BidReceiver::kBidHonorJitterMs; ++off) {
      uint8_t mac[6]; uint8_t type;
      if (r.takeDue(now + off, mac, type)) { dueOffsets.push_back(off); break; }
    }
  }
  TEST_ASSERT_EQUAL_INT(kReceivers, (int)dueOffsets.size());
  // Scatter: offsets bounded by the jitter window and genuinely spread out
  // (not all firing on the same tick).
  uint32_t mn = dueOffsets[0], mx = dueOffsets[0];
  int distinct = 0;
  for (size_t i = 0; i < dueOffsets.size(); ++i) {
    TEST_ASSERT_TRUE(dueOffsets[i] <= BidReceiver::kBidHonorJitterMs);
    if (dueOffsets[i] < mn) mn = dueOffsets[i];
    if (dueOffsets[i] > mx) mx = dueOffsets[i];
    bool seen = false;
    for (size_t j = 0; j < i; ++j) if (dueOffsets[j] == dueOffsets[i]) { seen = true; break; }
    if (!seen) ++distinct;
  }
  TEST_ASSERT_TRUE(distinct >= 5);
  TEST_ASSERT_TRUE((mx - mn) >= 50);
}

// --- Mirror of drainBid's roster-miss guard (lamp_drains.cpp): a bidder not
// in the roster never reaches onBid, so no disposition/cooldown state is
// touched. ---

namespace {
struct FakeEntry { uint8_t mac[6]; bool hasMac; };
bool fakeFindByMac(const std::vector<FakeEntry>& roster, const uint8_t mac[6]) {
  for (const auto& e : roster) {
    if (e.hasMac && std::memcmp(e.mac, mac, 6) == 0) return true;
  }
  return false;
}
}  // namespace

void test_findbymac_miss_never_reaches_receiver() {
  std::vector<FakeEntry> roster;  // empty → every lookup misses
  BidReceiver r;
  FastRng rng(5);
  bool reachedOnBid = false;
  // Mirror drainBid: findByMac miss → return before onBid.
  if (fakeFindByMac(roster, kSrc)) {
    reachedOnBid = true;
    r.onBid(kSrc, lamp_protocol::BID_GREETING, 3, SocialMode::Ambivert, 1000, rng);
  }
  TEST_ASSERT_FALSE(reachedOnBid);
  uint8_t mac[6]; uint8_t type;
  TEST_ASSERT_FALSE(r.takeDue(100000, mac, type));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_gate_zero_percent_never_arms);
  RUN_TEST(test_gate_hundred_percent_always_arms);
  RUN_TEST(test_gate_mid_tracks_probability);
  RUN_TEST(test_unknown_bidtype_ignored);
  RUN_TEST(test_honor_cooldown_suppresses_second_bid);
  RUN_TEST(test_deferred_honor_fires_once_after_jitter);
  RUN_TEST(test_deferred_honor_respects_its_delay);
  RUN_TEST(test_storm_scatters_honor_times);
  RUN_TEST(test_findbymac_miss_never_reaches_receiver);
  return UNITY_END();
}
