// Native tests for WispRoster — multi-wisp coordination logic.
//
// Pins the contract:
//   - Single wisp, no peers → claims every observed lamp regardless of RSSI.
//   - Two wisps, ours is >= 5 dB stronger → we claim.
//   - Two wisps, peer is >= 5 dB stronger → we drop.
//   - Within ±5 dB hysteresis band → last-tick owner keeps it.
//   - Within hysteresis AND both currently claiming → lower MAC tiebreak.
//   - Peer goes silent → its entries age out after 20s → survivor adopts.
//   - RSSI jitter inside hysteresis → no claim flip (no flicker).
//   - INT8_MIN lamp RSSI (never measured) → never claimed.
//   - A claimed lamp whose direct RSSI aged out is held while no peer hears it
//     directly, advertised without a signal, and handed to a peer that hears
//     it; a lamp heard directly within the window stays claimed.
//   - setLampPaint stores on the matched entry; no-op for unclaimed mac.
//   - recomputeClaims carries paint on a stable claim.
//   - STATE packs every painted claim (black included) with its W plane and
//     omits a claim with no paint assigned yet.
//   - Full claim set (up to the wire cap) packs into one broadcast frame,
//     contested entries included.
//   - A drift step fades in STATE over the length the fade setting picks,
//     from the colors the lamp shows; a plain paint ends the fade at once.

#include <unity.h>

#include <climits>
#include <cstdint>
#include <cstring>

#include "fleet/wisp_roster.hpp"
#include "paint/drift.hpp"
#include "paint/paint_distributor.hpp"
#include "status/state_frame.hpp"
#include "wire/lamp_protocol.hpp"

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

const uint8_t kColor[4] = {1, 2, 3, 4};

void hearX(wisp::LampInventory& inv, int8_t rssi, uint32_t nowMs) {
  inv.recordHello(kLampX, "x", kColor, kColor, 1, nowMs, rssi);
}

// One 2 s claim tick as main.cpp runs it, with the 30 s prune due when
// `prune` is set.
void claimTick(wisp::LampInventory& inv, wisp::WispRoster& r, uint32_t nowMs,
               bool prune) {
  if (prune) inv.prune(nowMs, LAMP_PRUNE_TIME_MS);
  wisp::LampObservation o[wisp::WISP_ROSTER_MAX_LAMPS];
  const size_t n = inv.copyObservations(o, wisp::WISP_ROSTER_MAX_LAMPS);
  r.recomputeClaims(o, n, nowMs);
}

// Both wisps claim a lamp until the first CLAIM exchange; by this tick the
// arbitration has settled.
constexpr uint32_t kSettledMs = 5000;

// Deliver `from`'s CLAIM frame entries to `to`.
void sendClaim(wisp::WispRoster& from, const uint8_t fromMac[6],
               wisp::WispRoster& to, uint32_t nowMs) {
  uint8_t buf[wisp::WISP_ROSTER_MAX_LAMPS * 7];
  const size_t n = from.snapshotClaimsForBroadcast(buf, sizeof(buf));
  to.recordPeerClaim(fromMac, buf, static_cast<uint8_t>(n), nowMs);
}

int8_t advertisedRssi(wisp::WispRoster& r) {
  uint8_t buf[7];
  TEST_ASSERT_EQUAL_size_t(1, r.snapshotClaimsForBroadcast(buf, sizeof(buf)));
  return static_cast<int8_t>(buf[6]);
}

wisp::LampObservation obs(const uint8_t mac[6], int8_t rssi) {
  wisp::LampObservation o{};
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
  wisp::LampObservation lamps[] = {
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

  // Tick 2: jump forward past the 20 s aging window without any further
  // peer broadcasts. Peer entries get pruned; we adopt X.
  lamps = obs(kLampX, -75);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/22000);
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

// A peer heard 5 s before the millis() wrap still owns its lamp 10 s after
// it, then ages out once its silence passes the 20 s window.
void test_peer_ages_out_across_the_wrap(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  uint8_t entries[7];
  packEntry(entries, kLampX, -55);
  r.recordPeerClaim(kPeerLowMac, entries, 1, /*nowMs=*/UINT32_MAX - 4999);
  auto lamps = obs(kLampX, -75);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/10000);
  TEST_ASSERT_FALSE(r.claims(kLampX));
  r.recomputeClaims(&lamps, 1, /*nowMs=*/15001);
  TEST_ASSERT_TRUE(r.claims(kLampX));
}

// A full peer table heard before the wrap; peer 0, the first heard, is heard
// again after it. The next new peer evicts peer 1, now the stalest, so its
// lamp falls to us and peer 0's does not.
void test_peer_eviction_keeps_a_peer_heard_after_the_wrap(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  const uint32_t start = UINT32_MAX - 1000;
  uint8_t lampMacs[wisp::WISP_ROSTER_MAX_PEERS + 1][6];
  wisp::LampObservation lamps[wisp::WISP_ROSTER_MAX_PEERS + 1];
  auto claim = [&](size_t i, uint32_t nowMs) {
    const uint8_t peer[6] = {0x10, 0x11, 0x22, 0x33, 0x44, static_cast<uint8_t>(i)};
    uint8_t entries[7];
    packEntry(entries, lampMacs[i], -50);
    r.recordPeerClaim(peer, entries, 1, nowMs);
  };
  for (size_t i = 0; i <= wisp::WISP_ROSTER_MAX_PEERS; i++) {
    const uint8_t m[6] = {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, static_cast<uint8_t>(i)};
    std::memcpy(lampMacs[i], m, 6);
    lamps[i] = obs(lampMacs[i], -80);
  }
  for (size_t i = 0; i < wisp::WISP_ROSTER_MAX_PEERS; i++) {
    claim(i, start + static_cast<uint32_t>(i) * 10);
  }
  claim(0, 100);
  claim(wisp::WISP_ROSTER_MAX_PEERS, 200);

  r.recomputeClaims(lamps, wisp::WISP_ROSTER_MAX_PEERS + 1, /*nowMs=*/300);
  TEST_ASSERT_FALSE(r.claims(lampMacs[0]));
  TEST_ASSERT_TRUE(r.claims(lampMacs[1]));
  TEST_ASSERT_FALSE(r.claims(lampMacs[wisp::WISP_ROSTER_MAX_PEERS]));
}

void test_never_measured_rssi_skips_claim(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  auto lamps = obs(kLampX, INT8_MIN);
  r.recomputeClaims(&lamps, 1, /*nowMs=*/1000);
  TEST_ASSERT_FALSE(r.claims(kLampX));
  TEST_ASSERT_EQUAL_size_t(0, r.claimedCount());
}

// Lamp X sits near wisp A (-50) and is heard by B (-72). It moves out of A's
// direct range: A hears it only through lamp relays, B still directly. For
// three HELLO intervals after A's last direct HELLO A keeps it; by the first
// prune past that plus two CLAIM rounds, B has it and A has let go.
void test_two_wisps_hand_off_a_lamp_whose_direct_signal_aged_out(void) {
  wisp::LampInventory invA, invB;
  wisp::WispRoster a, b;
  a.setSelfMac(kSelfMac);
  b.setSelfMac(kPeerHighMac);

  const uint32_t moved = 31000;
  const uint32_t lastDirect = moved - lamp_protocol::kLampHelloIntervalMs;
  const uint32_t windowEnd = lastDirect + 3 * lamp_protocol::kLampHelloIntervalMs;
  const uint32_t handedOffBy = windowEnd + 30000 + 8000;
  bool aClaimedInWindow = true;
  for (uint32_t t = 1000; t <= moved + 150000; t += 2000) {
    if ((t - 1000) % lamp_protocol::kLampHelloIntervalMs == 0) {
      hearX(invA, t < moved ? -50 : INT8_MIN, t);
      hearX(invB, -72, t);
    }
    const bool prune = (t - 1000) % 30000 == 0;
    claimTick(invA, a, t, prune);
    claimTick(invB, b, t, prune);
    if ((t - 1000) % 4000 == 0) {
      sendClaim(a, kSelfMac, b, t);
      sendClaim(b, kPeerHighMac, a, t);
    }
    if (t <= windowEnd && !a.claims(kLampX)) aClaimedInWindow = false;
    if (t >= kSettledMs && t < moved) TEST_ASSERT_FALSE(b.claims(kLampX));
    if (t >= handedOffBy) {
      TEST_ASSERT_FALSE_MESSAGE(a.claims(kLampX), "A still holds past the window");
      TEST_ASSERT_TRUE_MESSAGE(b.claims(kLampX), "B has not taken over");
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(aClaimedInWindow, "A let go inside the window");
}

// A hears X directly only every third HELLO (two lost in a row), B hears every
// one; A stays the owner throughout and B never takes it.
void test_lamp_heard_directly_within_window_stays_claimed(void) {
  wisp::LampInventory invA, invB;
  wisp::WispRoster a, b;
  a.setSelfMac(kSelfMac);
  b.setSelfMac(kPeerHighMac);

  hearX(invA, -50, 1000);
  for (uint32_t t = 1000; t <= 400000; t += 2000) {
    if (t >= 7000 && (t - 7000) % (3 * lamp_protocol::kLampHelloIntervalMs) == 0) {
      hearX(invA, -50, t);
    }
    if ((t - 1000) % lamp_protocol::kLampHelloIntervalMs == 0) hearX(invB, -72, t);
    const bool prune = (t - 1000) % 30000 == 0;
    claimTick(invA, a, t, prune);
    claimTick(invB, b, t, prune);
    if ((t - 1000) % 4000 == 0) {
      sendClaim(a, kSelfMac, b, t);
      sendClaim(b, kPeerHighMac, a, t);
    }
    if (t < kSettledMs) continue;
    TEST_ASSERT_TRUE(a.claims(kLampX));
    TEST_ASSERT_FALSE(b.claims(kLampX));
  }
}

// Single wisp: a lamp that goes silent (an OTA receiver stops sending HELLO)
// stays claimed after its direct RSSI ages out, advertised without a signal.
void test_aged_out_claim_held_without_peer(void) {
  wisp::LampInventory inv;
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  hearX(inv, -64, 1000);
  claimTick(inv, r, 1000, false);
  TEST_ASSERT_TRUE(r.claims(kLampX));
  TEST_ASSERT_EQUAL_INT8(-64, advertisedRssi(r));

  for (uint32_t t = 3000; t <= 1000 + LAMP_PRUNE_TIME_MS; t += 2000) {
    claimTick(inv, r, t, (t - 1000) % 30000 == 0);
    TEST_ASSERT_TRUE(r.claims(kLampX));
  }
  TEST_ASSERT_EQUAL_INT8(INT8_MIN, advertisedRssi(r));
}

void test_snapshot_for_broadcast_packs_correctly(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  wisp::LampObservation lamps[] = {
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

// --- composite frame tests ---

namespace {

void fleetMac(uint8_t out[6], uint8_t last) {
  const uint8_t m[6] = {0xCC, 0x00, 0x00, 0x00, 0x00, last};
  std::memcpy(out, m, 6);
}

// True when `mac` appears in a packed claim buffer.
bool bufHasMac(const uint8_t* buf, size_t entries, const uint8_t mac[6]) {
  for (size_t i = 0; i < entries; ++i) {
    const uint8_t* entry = buf + i * lamp_protocol::WISP_CLAIM_ENTRY_SIZE;
    if (std::memcmp(entry, mac, 6) == 0) return true;
  }
  return false;
}

}  // namespace

void test_full_claim_set_fits_one_frame(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  constexpr size_t kFleet = wisp::WISP_ROSTER_MAX_LAMPS;
  wisp::LampObservation lamps[kFleet];
  for (size_t i = 0; i < kFleet; ++i) {
    fleetMac(lamps[i].mac, static_cast<uint8_t>(i + 1));
    lamps[i].rssi = -60;
  }
  r.recomputeClaims(lamps, kFleet, /*nowMs=*/1000);
  TEST_ASSERT_EQUAL_size_t(kFleet, r.claimedCount());

  uint8_t buf[lamp_protocol::kMaxWispClaimEntries *
              lamp_protocol::WISP_CLAIM_ENTRY_SIZE];
  const size_t n = r.snapshotClaimsForBroadcast(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_size_t(kFleet, n);
  for (size_t i = 0; i < kFleet; ++i) {
    uint8_t m[6];
    fleetMac(m, static_cast<uint8_t>(i + 1));
    TEST_ASSERT_TRUE_MESSAGE(bufHasMac(buf, n, m),
                             "claimed lamp missing from the frame");
  }
}

void test_all_contested_entries_in_single_frame(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  // Peer also lists X and Y (weaker) → contested but still ours.
  uint8_t entries[2 * 7];
  packEntry(entries,     kLampX, -75);
  packEntry(entries + 7, kLampY, -75);
  r.recordPeerClaim(kPeerHighMac, entries, 2, /*nowMs=*/1000);

  constexpr size_t kFleet = 40;
  wisp::LampObservation lamps[kFleet + 2];
  lamps[0] = obs(kLampX, -60);
  lamps[1] = obs(kLampY, -60);
  for (size_t i = 0; i < kFleet; ++i) {
    fleetMac(lamps[i + 2].mac, static_cast<uint8_t>(i + 1));
    lamps[i + 2].rssi = -60;
  }
  r.recomputeClaims(lamps, kFleet + 2, /*nowMs=*/1000);
  TEST_ASSERT_EQUAL_size_t(kFleet + 2, r.claimedCount());

  uint8_t buf[lamp_protocol::kMaxWispClaimEntries *
              lamp_protocol::WISP_CLAIM_ENTRY_SIZE];
  const size_t n = r.snapshotClaimsForBroadcast(buf, sizeof(buf));
  TEST_ASSERT_TRUE_MESSAGE(bufHasMac(buf, n, kLampX),
                           "contested lamp X missing from the frame");
  TEST_ASSERT_TRUE_MESSAGE(bufHasMac(buf, n, kLampY),
                           "contested lamp Y missing from the frame");
}

void test_snapshot_state_packs_full_set_with_globals(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);

  constexpr size_t kFleet = 40;  // whole set rides one STATE frame, no window
  static_assert(kFleet <= lamp_protocol::WISP_STATE_MAX_ENTRIES, "fits STATE");
  wisp::LampObservation lamps[kFleet];
  for (size_t i = 0; i < kFleet; ++i) {
    fleetMac(lamps[i].mac, static_cast<uint8_t>(i + 1));
    lamps[i].rssi = -60;
  }
  r.recomputeClaims(lamps, kFleet, /*nowMs=*/1000);

  const uint8_t base[4]  = {0x30, 0x31, 0x32, 0x00};
  const uint8_t shade[4] = {0x40, 0x41, 0x42, 0x00};
  uint8_t firstMac[6];
  fleetMac(firstMac, 1);
  // Color every claimed lamp so the whole set packs (the snapshot skips
  // uncolored claims); firstMac's entry is checked below.
  for (size_t i = 0; i < kFleet; ++i) {
    uint8_t mac[6];
    fleetMac(mac, static_cast<uint8_t>(i + 1));
    r.setLampPaint(mac, base, shade);
  }

  uint8_t entries[lamp_protocol::WISP_STATE_MAX_ENTRIES *
                  lamp_protocol::WISP_STATE_ENTRY_SIZE];
  uint8_t w[lamp_protocol::WISP_STATE_MAX_ENTRIES *
            lamp_protocol::WISP_STATE_W_SIZE];
  const size_t n =
      r.snapshotStateForBroadcast(entries, sizeof(entries), w, sizeof(w), 1000);
  TEST_ASSERT_EQUAL_size_t(kFleet, n);

  const uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t frame[lamp_protocol::WISP_STATE_MAX_SIZE];
  const size_t len = lamp_protocol::buildWispState(
      frame, sizeof(frame), /*seq=*/7, srcMac, /*brightness=*/80,
      /*driftRateMs=*/120000, /*presenceFlags=*/0x05,
      entries, w, static_cast<uint8_t>(n));
  TEST_ASSERT_TRUE(len > 0);

  lamp_protocol::ParsedWispState out;
  TEST_ASSERT_TRUE(lamp_protocol::parseWispState(frame, len, out));
  TEST_ASSERT_EQUAL_UINT8(80, out.brightness);
  TEST_ASSERT_EQUAL_UINT32(120000, out.driftRateMs);
  TEST_ASSERT_EQUAL_UINT8(0x05, out.presenceFlags);
  TEST_ASSERT_EQUAL_size_t(kFleet, out.count);

  const uint8_t* firstEntry = nullptr;
  for (size_t i = 0; i < out.count; ++i) {
    if (std::memcmp(&out.entries[i * 12], firstMac, 6) == 0) {
      firstEntry = &out.entries[i * 12];
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(firstEntry);
  TEST_ASSERT_EQUAL_UINT8(0x30, firstEntry[6]);
  TEST_ASSERT_EQUAL_UINT8(0x42, firstEntry[11]);
}

// Pack a STATE snapshot and return the entry count; `entries` / `w` sized for
// the full cap.
size_t snapshotState(wisp::WispRoster& r, uint8_t* entries, uint8_t* w,
                     uint32_t nowMs = 2000) {
  return r.snapshotStateForBroadcast(
      entries,
      lamp_protocol::WISP_STATE_MAX_ENTRIES * lamp_protocol::WISP_STATE_ENTRY_SIZE,
      w, lamp_protocol::WISP_STATE_MAX_ENTRIES * lamp_protocol::WISP_STATE_W_SIZE,
      nowMs);
}

// Index of `mac` in a packed STATE entry block, or -1.
int stateIndexOf(const uint8_t* entries, size_t n, const uint8_t mac[6]) {
  for (size_t i = 0; i < n; ++i) {
    if (std::memcmp(&entries[i * lamp_protocol::WISP_STATE_ENTRY_SIZE], mac, 6) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

uint8_t gEntries[lamp_protocol::WISP_STATE_MAX_ENTRIES *
                 lamp_protocol::WISP_STATE_ENTRY_SIZE];
uint8_t gW[lamp_protocol::WISP_STATE_MAX_ENTRIES *
           lamp_protocol::WISP_STATE_W_SIZE];

void test_set_lamp_paint_stores_on_claimed_entry(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  auto lamp = obs(kLampX, -65);
  r.recomputeClaims(&lamp, 1, /*nowMs=*/1000);
  TEST_ASSERT_TRUE(r.claims(kLampX));

  const uint8_t base[4]  = {0x11, 0x22, 0x33, 0x00};
  const uint8_t shade[4] = {0xAA, 0xBB, 0xCC, 0x00};
  r.setLampPaint(kLampX, base, shade);

  const size_t n = snapshotState(r, gEntries, gW);
  TEST_ASSERT_EQUAL_size_t(1, n);
  const uint8_t expected[6] = {0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC};
  TEST_ASSERT_EQUAL_MEMORY(expected, &gEntries[6], 6);
}

void test_set_lamp_paint_noop_for_unclaimed_mac(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  const uint8_t base[4]  = {0x11, 0x22, 0x33, 0x00};
  const uint8_t shade[4] = {0xAA, 0xBB, 0xCC, 0x00};
  r.setLampPaint(kLampX, base, shade);
  TEST_ASSERT_EQUAL_size_t(0, snapshotState(r, gEntries, gW));
}

void test_recompute_preserves_paint_on_stable_claim(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  auto lamp = obs(kLampX, -65);
  r.recomputeClaims(&lamp, 1, /*nowMs=*/1000);

  const uint8_t base[4]  = {0x10, 0x20, 0x30, 0x00};
  const uint8_t shade[4] = {0xA0, 0xB0, 0xC0, 0x00};
  r.setLampPaint(kLampX, base, shade);

  lamp = obs(kLampX, -65);
  r.recomputeClaims(&lamp, 1, /*nowMs=*/2000);
  TEST_ASSERT_TRUE(r.claims(kLampX));

  const size_t n = snapshotState(r, gEntries, gW);
  TEST_ASSERT_EQUAL_size_t(1, n);
  const uint8_t expected[6] = {0x10, 0x20, 0x30, 0xA0, 0xB0, 0xC0};
  TEST_ASSERT_EQUAL_MEMORY(expected, &gEntries[6], 6);
}

void test_snapshot_state_skips_unassigned_claims(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  wisp::LampObservation lamps[] = {obs(kLampX, -60), obs(kLampY, -70)};
  r.recomputeClaims(lamps, 2, /*nowMs=*/1000);

  const uint8_t base[4]  = {0x10, 0x20, 0x30, 0x00};
  const uint8_t shade[4] = {0x40, 0x50, 0x60, 0x00};
  r.setLampPaint(kLampX, base, shade);

  const size_t n = snapshotState(r, gEntries, gW);
  TEST_ASSERT_EQUAL_size_t(1, n);
  TEST_ASSERT_TRUE(stateIndexOf(gEntries, n, kLampX) >= 0);
  TEST_ASSERT_EQUAL_INT(-1, stateIndexOf(gEntries, n, kLampY));

  // Still unassigned after another recompute.
  r.recomputeClaims(lamps, 2, /*nowMs=*/2000);
  const size_t n2 = snapshotState(r, gEntries, gW);
  TEST_ASSERT_EQUAL_size_t(1, n2);
  TEST_ASSERT_EQUAL_INT(-1, stateIndexOf(gEntries, n2, kLampY));
}

void test_snapshot_state_packs_black_claim(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  wisp::LampObservation lamps[] = {obs(kLampX, -60), obs(kLampY, -70)};
  r.recomputeClaims(lamps, 2, /*nowMs=*/1000);

  const uint8_t black[4] = {0, 0, 0, 0};
  const uint8_t base[4]  = {0x10, 0x20, 0x30, 0x00};
  r.setLampPaint(kLampX, black, black);
  r.setLampPaint(kLampY, base, black);

  size_t n = snapshotState(r, gEntries, gW);
  TEST_ASSERT_EQUAL_size_t(2, n);
  const int x = stateIndexOf(gEntries, n, kLampX);
  TEST_ASSERT_TRUE(x >= 0);
  const uint8_t zeros[6] = {0};
  TEST_ASSERT_EQUAL_MEMORY(zeros,
                           &gEntries[x * lamp_protocol::WISP_STATE_ENTRY_SIZE + 6], 6);
  TEST_ASSERT_EQUAL_MEMORY(zeros, &gW[x * lamp_protocol::WISP_STATE_W_SIZE], 2);

  // A held claim keeps its black paint across recompute.
  r.recomputeClaims(lamps, 2, /*nowMs=*/2000);
  n = snapshotState(r, gEntries, gW);
  TEST_ASSERT_EQUAL_size_t(2, n);
  TEST_ASSERT_TRUE(stateIndexOf(gEntries, n, kLampX) >= 0);
}

void test_snapshot_state_reclaim_starts_unassigned(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  wisp::LampObservation lamps[] = {obs(kLampX, -60), obs(kLampY, -70)};
  r.recomputeClaims(lamps, 2, /*nowMs=*/1000);
  const uint8_t base[4] = {0x10, 0x20, 0x30, 0x40};
  r.setLampPaint(kLampX, base, base);
  r.setLampPaint(kLampY, base, base);

  // Y drops out of the claim set, then returns: a fresh claim with no paint.
  r.recomputeClaims(lamps, 1, /*nowMs=*/2000);
  r.recomputeClaims(lamps, 2, /*nowMs=*/3000);
  const size_t n = snapshotState(r, gEntries, gW);
  TEST_ASSERT_EQUAL_size_t(1, n);
  TEST_ASSERT_EQUAL_INT(-1, stateIndexOf(gEntries, n, kLampY));
}

void test_snapshot_state_carries_w_plane(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  wisp::LampObservation lamps[] = {obs(kLampX, -60), obs(kLampY, -70)};
  r.recomputeClaims(lamps, 2, /*nowMs=*/1000);

  // X is warm-white only; Y mixes RGB with a distinct W per surface.
  const uint8_t xBase[4]  = {0, 0, 0, 0xC8};
  const uint8_t xShade[4] = {0, 0, 0, 0x37};
  const uint8_t yBase[4]  = {0x11, 0x22, 0x33, 0x05};
  const uint8_t yShade[4] = {0x44, 0x55, 0x66, 0x9A};
  r.setLampPaint(kLampX, xBase, xShade);
  r.setLampPaint(kLampY, yBase, yShade);

  const size_t n = snapshotState(r, gEntries, gW);
  TEST_ASSERT_EQUAL_size_t(2, n);

  const uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t frame[lamp_protocol::WISP_STATE_MAX_SIZE];
  const size_t len = lamp_protocol::buildWispState(
      frame, sizeof(frame), /*seq=*/3, srcMac, 100, 60000, 0, gEntries, gW,
      static_cast<uint8_t>(n));
  TEST_ASSERT_TRUE(len > 0);
  lamp_protocol::ParsedWispState out;
  TEST_ASSERT_TRUE(lamp_protocol::parseWispState(frame, len, out));
  TEST_ASSERT_NOT_NULL(out.w);

  const int x = stateIndexOf(out.entries, out.count, kLampX);
  const int y = stateIndexOf(out.entries, out.count, kLampY);
  TEST_ASSERT_TRUE(x >= 0 && y >= 0);
  TEST_ASSERT_EQUAL_UINT8(0xC8, out.w[x * 2]);
  TEST_ASSERT_EQUAL_UINT8(0x37, out.w[x * 2 + 1]);
  TEST_ASSERT_EQUAL_UINT8(0x05, out.w[y * 2]);
  TEST_ASSERT_EQUAL_UINT8(0x9A, out.w[y * 2 + 1]);
  TEST_ASSERT_EQUAL_UINT8(0x33, out.entries[y * 12 + 8]);
  TEST_ASSERT_EQUAL_UINT8(0x44, out.entries[y * 12 + 9]);
}

void test_store_tuple_paint_carries_w(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  auto lamp = obs(kLampX, -60);
  r.recomputeClaims(&lamp, 1, /*nowMs=*/1000);

  wisp::ColorTuple t;
  t.r[0] = 0x11; t.g[0] = 0x22; t.b[0] = 0x33; t.w[0] = 0xC8;
  t.r[1] = 0x44; t.g[1] = 0x55; t.b[1] = 0x66; t.w[1] = 0x37;
  wisp::storeTuplePaint(r, kLampX, t);

  const size_t n = snapshotState(r, gEntries, gW);
  TEST_ASSERT_EQUAL_size_t(1, n);
  const uint8_t wantRgb[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  TEST_ASSERT_EQUAL_MEMORY(wantRgb, &gEntries[6], 6);
  TEST_ASSERT_EQUAL_UINT8(0xC8, gW[0]);
  TEST_ASSERT_EQUAL_UINT8(0x37, gW[1]);
}

// Paint `count` fleet lamps so each is a packable claim.
void claimAndPaint(wisp::WispRoster& r, size_t count) {
  wisp::LampObservation lamps[16];
  for (size_t i = 0; i < count; ++i) {
    fleetMac(lamps[i].mac, static_cast<uint8_t>(i + 1));
    lamps[i].rssi = -60;
  }
  r.recomputeClaims(lamps, count, /*nowMs=*/1000);
  const uint8_t base[4] = {1, 2, 3, 4};
  for (size_t i = 0; i < count; ++i) r.setLampPaint(lamps[i].mac, base, base);
}

void test_snapshot_state_w_capacity_bounds_entries(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  claimAndPaint(r, 10);

  // Room for 10 entries but W pairs for only 3; bytes past wCap must stay put.
  constexpr size_t kWCap = 3 * lamp_protocol::WISP_STATE_W_SIZE;
  uint8_t entries[10 * lamp_protocol::WISP_STATE_ENTRY_SIZE];
  uint8_t w[10 * lamp_protocol::WISP_STATE_W_SIZE];
  std::memset(w, 0xEE, sizeof(w));
  const size_t n =
      r.snapshotStateForBroadcast(entries, sizeof(entries), w, kWCap, 1000);
  TEST_ASSERT_EQUAL_size_t(3, n);
  for (size_t i = kWCap; i < sizeof(w); ++i) TEST_ASSERT_EQUAL_UINT8(0xEE, w[i]);
}

void test_snapshot_state_null_w_packs_nothing(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  claimAndPaint(r, 2);

  uint8_t entries[2 * lamp_protocol::WISP_STATE_ENTRY_SIZE];
  std::memset(entries, 0xEE, sizeof(entries));
  const size_t n = r.snapshotStateForBroadcast(
      entries, sizeof(entries), nullptr, 2 * lamp_protocol::WISP_STATE_W_SIZE,
      1000);
  TEST_ASSERT_EQUAL_size_t(0, n);
  for (uint8_t b : entries) TEST_ASSERT_EQUAL_UINT8(0xEE, b);
}

void test_build_state_frame_carries_w_plane(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  auto lamp = obs(kLampX, -60);
  r.recomputeClaims(&lamp, 1, /*nowMs=*/1000);
  const uint8_t base[4]  = {0x11, 0x22, 0x33, 0xC8};
  const uint8_t shade[4] = {0x44, 0x55, 0x66, 0x37};
  r.setLampPaint(kLampX, base, shade);

  const uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t frame[lamp_protocol::WISP_STATE_MAX_SIZE];
  const size_t len = wisp::buildStateFrame(
      r, frame, sizeof(frame), /*seq=*/9, srcMac, /*brightness=*/90,
      /*driftRateMs=*/30000, /*presenceFlags=*/0x01, /*painting=*/true,
      /*nowMs=*/1000);
  lamp_protocol::ParsedWispState out;
  TEST_ASSERT_TRUE(lamp_protocol::parseWispState(frame, len, out));
  TEST_ASSERT_EQUAL_UINT8(1, out.count);
  TEST_ASSERT_EQUAL_MEMORY(kLampX, out.entries, 6);
  TEST_ASSERT_EQUAL_UINT8(0x33, out.entries[8]);
  TEST_ASSERT_NOT_NULL(out.w);
  TEST_ASSERT_EQUAL_UINT8(0xC8, out.w[0]);
  TEST_ASSERT_EQUAL_UINT8(0x37, out.w[1]);
  TEST_ASSERT_EQUAL_UINT8(90, out.brightness);
  TEST_ASSERT_EQUAL_UINT32(30000, out.driftRateMs);
  TEST_ASSERT_EQUAL_UINT8(0x01, out.presenceFlags);
}

void test_build_state_frame_off_packs_no_entries(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  auto lamp = obs(kLampX, -60);
  r.recomputeClaims(&lamp, 1, /*nowMs=*/1000);
  const uint8_t base[4] = {1, 2, 3, 4};
  r.setLampPaint(kLampX, base, base);

  const uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t frame[lamp_protocol::WISP_STATE_MAX_SIZE];
  const size_t len = wisp::buildStateFrame(r, frame, sizeof(frame), 9, srcMac,
                                           90, 30000, 0, /*painting=*/false,
                                           /*nowMs=*/1000);
  lamp_protocol::ParsedWispState out;
  TEST_ASSERT_TRUE(lamp_protocol::parseWispState(frame, len, out));
  TEST_ASSERT_EQUAL_UINT8(0, out.count);
}

// Drift fixture: each channel moves, both directions, away from 0 and 255.
const uint8_t kFromBase[4]   = {10, 200, 40, 5};
const uint8_t kFromShade[4]  = {150, 30, 220, 60};
const uint8_t kToBase[4]     = {180, 20, 120, 90};
const uint8_t kToShade[4]    = {40, 170, 70, 10};
const uint8_t kOtherBase[4]  = {90, 90, 200, 33};
const uint8_t kOtherShade[4] = {12, 240, 100, 77};
constexpr uint32_t kDriftStartMs = 3000;

// kLampX claimed and painted kFrom, then drifted to kTo at kDriftStartMs.
void claimPaintDrift(wisp::WispRoster& r, uint32_t fadeMs) {
  r.setSelfMac(kSelfMac);
  auto lamp = obs(kLampX, -60);
  r.recomputeClaims(&lamp, 1, /*nowMs=*/1000);
  r.setLampPaint(kLampX, kFromBase, kFromShade);
  r.setLampDrift(kLampX, kToBase, kToShade, kDriftStartMs, fadeMs);
}

// kLampX's RGBW base and shade as a lamp reads them from the STATE frame the
// wisp builds at nowMs.
void stateColorsAt(wisp::WispRoster& r, uint32_t nowMs, uint8_t base[4],
                   uint8_t shade[4]) {
  const uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t frame[lamp_protocol::WISP_STATE_MAX_SIZE];
  const size_t len = wisp::buildStateFrame(
      r, frame, sizeof(frame), /*seq=*/1, srcMac, /*brightness=*/100,
      /*driftRateMs=*/120000, /*presenceFlags=*/0x01, /*painting=*/true, nowMs);
  lamp_protocol::ParsedWispState out;
  TEST_ASSERT_TRUE(lamp_protocol::parseWispState(frame, len, out));
  const uint8_t* e = lamp_protocol::findWispStateEntry(out.entries, out.count, kLampX);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_NOT_NULL(out.w);
  const size_t i = static_cast<size_t>(e - out.entries) /
                   lamp_protocol::WISP_STATE_ENTRY_SIZE;
  for (size_t c = 0; c < 3; ++c) {
    base[c] = e[6 + c];
    shade[c] = e[9 + c];
  }
  base[3] = out.w[i * lamp_protocol::WISP_STATE_W_SIZE];
  shade[3] = out.w[i * lamp_protocol::WISP_STATE_W_SIZE + 1];
}

void assertStateColors(wisp::WispRoster& r, uint32_t nowMs,
                       const uint8_t base[4], const uint8_t shade[4]) {
  uint8_t gotBase[4];
  uint8_t gotShade[4];
  stateColorsAt(r, nowMs, gotBase, gotShade);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(base, gotBase, 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(shade, gotShade, 4);
}

void assertStrictlyBetween(const uint8_t a[4], const uint8_t b[4],
                           const uint8_t got[4]) {
  for (size_t c = 0; c < 4; ++c) {
    const uint8_t lo = a[c] < b[c] ? a[c] : b[c];
    const uint8_t hi = a[c] < b[c] ? b[c] : a[c];
    TEST_ASSERT_TRUE(got[c] > lo && got[c] < hi);
  }
}

// Mid-fade: each channel strictly between the drift's start and target.
void assertMidDrift(wisp::WispRoster& r, uint32_t nowMs) {
  uint8_t base[4];
  uint8_t shade[4];
  stateColorsAt(r, nowMs, base, shade);
  assertStrictlyBetween(kFromBase, kToBase, base);
  assertStrictlyBetween(kFromShade, kToShade, shade);
}

// kLampX claimed and painted kFrom, then one drift step to kTo at
// kDriftStartMs through the distributor's store path. Returns the fade length.
uint32_t claimPaintDriftStep(wisp::WispRoster& r, uint32_t intervalMs,
                             uint8_t fadePct, uint32_t rnd) {
  r.setSelfMac(kSelfMac);
  auto lamp = obs(kLampX, -60);
  r.recomputeClaims(&lamp, 1, /*nowMs=*/1000);
  r.setLampPaint(kLampX, kFromBase, kFromShade);
  wisp::ColorTuple t;
  t.r[0] = kToBase[0];  t.g[0] = kToBase[1];  t.b[0] = kToBase[2];  t.w[0] = kToBase[3];
  t.r[1] = kToShade[0]; t.g[1] = kToShade[1]; t.b[1] = kToShade[2]; t.w[1] = kToShade[3];
  return wisp::storeTupleDrift(r, kLampX, t, kDriftStartMs, intervalMs, fadePct, rnd);
}

void test_drift_fade_setting_changes_what_lamps_receive(void) {
  // Same interval and random draw: only the app's Fade setting differs.
  constexpr uint32_t kIntervalMs = 120000;
  constexpr uint32_t kRnd = 90000;
  wisp::WispRoster snappy;
  wisp::WispRoster flowing;
  const uint32_t snappyMs = claimPaintDriftStep(snappy, kIntervalMs, 0, kRnd);
  const uint32_t flowingMs = claimPaintDriftStep(flowing, kIntervalMs, 100, kRnd);
  TEST_ASSERT_TRUE(flowingMs > snappyMs);

  // A step starts from the colors the lamp shows; neither jumps.
  assertStateColors(snappy, kDriftStartMs, kFromBase, kFromShade);
  assertStateColors(flowing, kDriftStartMs, kFromBase, kFromShade);

  // When the snappy fade lands, the flowing one is still on its way.
  assertStateColors(snappy, kDriftStartMs + snappyMs, kToBase, kToShade);
  assertMidDrift(flowing, kDriftStartMs + snappyMs);

  assertStateColors(flowing, kDriftStartMs + flowingMs, kToBase, kToShade);
}

void test_set_lamp_paint_ends_drift_fade(void) {
  wisp::WispRoster r;
  claimPaintDrift(r, 60000);
  r.setLampPaint(kLampX, kOtherBase, kOtherShade);
  assertStateColors(r, kDriftStartMs + 30000, kOtherBase, kOtherShade);
}

void test_drift_retarget_starts_from_shown_colors(void) {
  wisp::WispRoster r;
  claimPaintDrift(r, 60000);
  const uint32_t midMs = kDriftStartMs + 30000;
  uint8_t shownBase[4];
  uint8_t shownShade[4];
  stateColorsAt(r, midMs, shownBase, shownShade);

  r.setLampDrift(kLampX, kOtherBase, kOtherShade, midMs, 60000);
  assertStateColors(r, midMs, shownBase, shownShade);
  assertStateColors(r, midMs + 60000, kOtherBase, kOtherShade);
}

void test_drift_on_unpainted_claim_shows_at_once(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  auto lamp = obs(kLampX, -60);
  r.recomputeClaims(&lamp, 1, /*nowMs=*/1000);
  r.setLampDrift(kLampX, kToBase, kToShade, kDriftStartMs, 60000);
  assertStateColors(r, kDriftStartMs, kToBase, kToShade);
}

void test_recompute_carries_drift_fade(void) {
  wisp::WispRoster r;
  claimPaintDrift(r, 60000);
  auto lamp = obs(kLampX, -60);
  r.recomputeClaims(&lamp, 1, kDriftStartMs + 10000);
  assertMidDrift(r, kDriftStartMs + 30000);
}

void test_recompute_retires_finished_drift_fade(void) {
  wisp::WispRoster r;
  claimPaintDrift(r, 20000);
  auto lamp = obs(kLampX, -60);
  r.recomputeClaims(&lamp, 1, kDriftStartMs + 25000);
  // One millis() wrap later the start stamp reads 5 s old again; the finished
  // fade must not replay.
  assertStateColors(r, kDriftStartMs + 5000, kToBase, kToShade);
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
  RUN_TEST(test_two_wisps_hand_off_a_lamp_whose_direct_signal_aged_out);
  RUN_TEST(test_lamp_heard_directly_within_window_stays_claimed);
  RUN_TEST(test_aged_out_claim_held_without_peer);
  RUN_TEST(test_peer_not_pruned_when_lastseen_ahead_of_now);
  RUN_TEST(test_peer_ages_out_across_the_wrap);
  RUN_TEST(test_peer_eviction_keeps_a_peer_heard_after_the_wrap);
  RUN_TEST(test_snapshot_for_broadcast_packs_correctly);
  RUN_TEST(test_full_claim_set_fits_one_frame);
  RUN_TEST(test_all_contested_entries_in_single_frame);
  RUN_TEST(test_snapshot_state_packs_full_set_with_globals);
  RUN_TEST(test_set_lamp_paint_stores_on_claimed_entry);
  RUN_TEST(test_set_lamp_paint_noop_for_unclaimed_mac);
  RUN_TEST(test_recompute_preserves_paint_on_stable_claim);
  RUN_TEST(test_snapshot_state_skips_unassigned_claims);
  RUN_TEST(test_snapshot_state_packs_black_claim);
  RUN_TEST(test_snapshot_state_reclaim_starts_unassigned);
  RUN_TEST(test_snapshot_state_carries_w_plane);
  RUN_TEST(test_store_tuple_paint_carries_w);
  RUN_TEST(test_snapshot_state_w_capacity_bounds_entries);
  RUN_TEST(test_snapshot_state_null_w_packs_nothing);
  RUN_TEST(test_build_state_frame_carries_w_plane);
  RUN_TEST(test_build_state_frame_off_packs_no_entries);
  RUN_TEST(test_drift_fade_setting_changes_what_lamps_receive);
  RUN_TEST(test_set_lamp_paint_ends_drift_fade);
  RUN_TEST(test_drift_retarget_starts_from_shown_colors);
  RUN_TEST(test_drift_on_unpainted_claim_shows_at_once);
  RUN_TEST(test_recompute_carries_drift_fade);
  RUN_TEST(test_recompute_retires_finished_drift_fade);
  return UNITY_END();
}
