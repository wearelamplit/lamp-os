// Native tests for WispRoster — multi-wisp coordination logic.
//
// Pins the contract:
//   - Single wisp, no peers → claims every observed lamp regardless of RSSI.
//   - Two wisps, ours is >= 5 dB stronger → we claim.
//   - Two wisps, peer is >= 5 dB stronger → we drop.
//   - Within ±5 dB hysteresis band → last-tick owner keeps it.
//   - Within hysteresis AND both currently claiming → lower MAC tiebreak.
//   - Peer goes silent → its entries age out after 10s → survivor adopts.
//   - RSSI jitter inside hysteresis → no claim flip (no flicker).
//   - INT8_MIN lamp RSSI (never measured) → never claimed.

#include <unity.h>

#include <climits>
#include <cstdint>
#include <cstring>

#include "WispRoster.h"

namespace {

constexpr int8_t kHyst = wisp::WISP_ROSTER_HYSTERESIS_DB;

uint8_t kSelfMac[6] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x55};
uint8_t kPeerLowMac[6]  = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}; // lower than self
uint8_t kPeerHighMac[6] = {0xFF, 0x11, 0x22, 0x33, 0x44, 0x55}; // higher than self
uint8_t kLampX[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0x01};
uint8_t kLampY[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0x02};

// Pack one peer entry as (lampMac[6], int8 rssi) in wire format.
void packEntry(uint8_t* buf, const uint8_t mac[6], int8_t rssi) {
  std::memcpy(buf, mac, 6);
  buf[6] = static_cast<uint8_t>(rssi);
}

wisp::WispRoster::LampObservation obs(const uint8_t mac[6], int8_t rssi) {
  wisp::WispRoster::LampObservation o{};
  std::memcpy(o.mac, mac, 6);
  o.rssi = rssi;
  return o;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_single_wisp_claims_everything(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  wisp::WispRoster::LampObservation lamps[] = {
      obs(kLampX, -65),
      obs(kLampY, -75),
  };
  r.recomputeClaims(lamps, 2, /*nowMs=*/1000);
  TEST_ASSERT_TRUE(r.claims(kLampX));
  TEST_ASSERT_TRUE(r.claims(kLampY));
  TEST_ASSERT_EQUAL_size_t(2, r.claimedCount());
}

void test_we_claim_when_strictly_closer(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  uint8_t entries[7];
  packEntry(entries, kLampX, -75); // peer's RSSI to X
  r.recordPeerClaim(kPeerLowMac, entries, 1, /*nowMs=*/1000);

  auto lamps = obs(kLampX, -60); // we hear X at -60 → 15 dB advantage
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_TRUE(r.claims(kLampX));
}

void test_we_drop_when_strictly_further(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  uint8_t entries[7];
  packEntry(entries, kLampX, -55); // peer hears X at -55
  r.recordPeerClaim(kPeerLowMac, entries, 1, /*nowMs=*/1000);

  auto lamps = obs(kLampX, -80); // we hear X at -80 → 25 dB DISadvantage
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_FALSE(r.claims(kLampX));
}

void test_hysteresis_keeps_last_owner_within_band(void) {
  // Hysteresis-only test: use a HIGHER-MAC peer so the simultaneous-
  // claim tiebreaker doesn't fire. We expect stickiness to keep us as
  // the owner when the peer comes in within the hysteresis band.
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  // Tick 1: peer hasn't broadcast yet. We claim X unconditionally.
  auto lamps = obs(kLampX, -65);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_TRUE(r.claims(kLampX));

  // Tick 2: higher-MAC peer broadcasts within hysteresis band.
  uint8_t entries[7];
  packEntry(entries, kLampX, -67);
  r.recordPeerClaim(kPeerHighMac, entries, 1, /*nowMs=*/1000);
  lamps = obs(kLampX, -65);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  // Within hysteresis + we own + peer has higher MAC → we keep it.
  TEST_ASSERT_TRUE(r.claims(kLampX));
}

void test_hysteresis_with_peer_owner_keeps_peer(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  // Tick 1: peer is meaningfully closer; we hand X to them.
  uint8_t entries[7];
  packEntry(entries, kLampX, -55);
  r.recordPeerClaim(kPeerLowMac, entries, 1, /*nowMs=*/1000);
  auto lamps = obs(kLampX, -75);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_FALSE(r.claims(kLampX));

  // Tick 2: peer drifts a bit closer to us — now within hysteresis.
  packEntry(entries, kLampX, -73); // 2 dB advantage to peer
  r.recordPeerClaim(kPeerLowMac, entries, 1, /*nowMs=*/2000);
  lamps = obs(kLampX, -75);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/2000);
  // Hysteresis keeps the peer as owner (we didn't claim last tick).
  TEST_ASSERT_FALSE(r.claims(kLampX));
}

void test_lower_mac_wins_simultaneous_claim_tiebreak(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  // Tick 1: we both observe X at the same time, no prior history.
  // We claim it unconditionally because the peer's broadcast hasn't
  // landed yet.
  auto lamps = obs(kLampX, -65);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_TRUE(r.claims(kLampX));

  // Tick 2: peer's broadcast lands — peer claims X too, at near-identical
  // RSSI. Hysteresis would keep us (last-tick owner), but if peer has the
  // LOWER MAC, lower-MAC tiebreak wins and we yield.
  uint8_t entries[7];
  packEntry(entries, kLampX, -67); // 2 dB peer-side, within hysteresis
  r.recordPeerClaim(kPeerLowMac, entries, 1, /*nowMs=*/2000);
  lamps = obs(kLampX, -65);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/2000);
  // Peer's MAC is lower → peer wins the tiebreak → we drop.
  TEST_ASSERT_FALSE(r.claims(kLampX));
}

void test_higher_mac_peer_loses_tiebreak(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  // Same setup, but peer has HIGHER mac. We should keep the claim.
  auto lamps = obs(kLampX, -65);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_TRUE(r.claims(kLampX));

  uint8_t entries[7];
  packEntry(entries, kLampX, -67);
  r.recordPeerClaim(kPeerHighMac, entries, 1, /*nowMs=*/2000);
  lamps = obs(kLampX, -65);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/2000);
  TEST_ASSERT_TRUE(r.claims(kLampX));
}

void test_peer_silence_ages_out_and_we_adopt(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  // Tick 1: peer claims X louder than us; we yield.
  uint8_t entries[7];
  packEntry(entries, kLampX, -55);
  r.recordPeerClaim(kPeerLowMac, entries, 1, /*nowMs=*/1000);
  auto lamps = obs(kLampX, -75);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_FALSE(r.claims(kLampX));

  // Tick 2: jump forward past the 10 s aging window without any further
  // peer broadcasts. Peer entries get pruned; we adopt X.
  lamps = obs(kLampX, -75);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/12000);
  TEST_ASSERT_TRUE(r.claims(kLampX));
}

void test_rssi_jitter_inside_hysteresis_no_flap(void) {
  // Higher-MAC peer so the simultaneous-claim tiebreaker doesn't fire —
  // this test is about hysteresis surviving RSSI jitter, not about
  // tiebreak resolution.
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  uint8_t entries[7];
  packEntry(entries, kLampX, -65);
  r.recordPeerClaim(kPeerHighMac, entries, 1, /*nowMs=*/1000);

  // We start ahead of peer — claim X.
  auto lamps = obs(kLampX, -60); // 5 dB advantage = right at the boundary
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_TRUE(r.claims(kLampX));

  // Jitter ±3 dB on subsequent ticks, peer steady. Claim should stick.
  for (int8_t delta = -3; delta <= 3; ++delta) {
    lamps = obs(kLampX, static_cast<int8_t>(-60 + delta));
    r.recomputeClaims(&lamps, 1, /*nowMs=*/2000 + static_cast<uint32_t>(delta + 3) * 100);
    TEST_ASSERT_TRUE_MESSAGE(r.claims(kLampX),
                             "claim flipped on RSSI jitter inside hysteresis");
  }
}

void test_peer_not_pruned_when_lastseen_ahead_of_now(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  uint8_t entries[7];
  packEntry(entries, kLampX, -65);
  r.recordPeerClaim(kPeerHighMac, entries, 1, /*nowMs=*/5000);
  auto lamps = obs(kLampX, -75);                // peer is closer → owns X
  r.recomputeClaims(&lamps, 1, /*nowMs=*/4000); // nowMs < lastSeenMs by 1s
  TEST_ASSERT_FALSE(r.claims(kLampX));          // buggy code prunes & adopts
}

void test_never_measured_rssi_skips_claim(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  auto lamps = obs(kLampX, INT8_MIN);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_FALSE(r.claims(kLampX));
  TEST_ASSERT_EQUAL_size_t(0, r.claimedCount());
}

void test_snapshot_for_broadcast_packs_correctly(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  wisp::WispRoster::LampObservation lamps[] = {
      obs(kLampX, -60),
      obs(kLampY, -70),
  };
  r.recomputeClaims(lamps, 2, /*nowMs=*/1000);

  uint8_t buf[14];  // 2 entries × 7 bytes
  const size_t n = r.snapshotClaimsForBroadcast(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_size_t(2, n);
  // Insertion order; matches observations[0] then [1].
  TEST_ASSERT_EQUAL_INT8(-60, static_cast<int8_t>(buf[6]));
  TEST_ASSERT_EQUAL_INT8(-70, static_cast<int8_t>(buf[13]));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_wisp_claims_everything);
  RUN_TEST(test_we_claim_when_strictly_closer);
  RUN_TEST(test_we_drop_when_strictly_further);
  RUN_TEST(test_hysteresis_keeps_last_owner_within_band);
  RUN_TEST(test_hysteresis_with_peer_owner_keeps_peer);
  RUN_TEST(test_lower_mac_wins_simultaneous_claim_tiebreak);
  RUN_TEST(test_higher_mac_peer_loses_tiebreak);
  RUN_TEST(test_peer_silence_ages_out_and_we_adopt);
  RUN_TEST(test_rssi_jitter_inside_hysteresis_no_flap);
  RUN_TEST(test_never_measured_rssi_skips_claim);
  RUN_TEST(test_peer_not_pruned_when_lastseen_ahead_of_now);
  RUN_TEST(test_snapshot_for_broadcast_packs_correctly);
  return UNITY_END();
}
