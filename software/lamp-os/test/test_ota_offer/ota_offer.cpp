// Pins the offer scan's pacing and its receive-first gate.

#include <unity.h>
#include <cstdint>
#include <vector>

#include "components/firmware/inter_session_quiet_hold.hpp"
#include "components/firmware/ota_offer.hpp"  // -I src on native

using lamp::InterSessionQuietHold;
using lamp::MeshPeer;
using lamp::OtaOfferBusy;
using lamp::OtaOfferThrottle;
using lamp::offerBusy;
using lamp::peerRunsNewerFirmware;

void setUp(void) {}
void tearDown(void) {}

void test_first_idle_tick_scans() {
  OtaOfferThrottle t;
  TEST_ASSERT_TRUE(t.due({}, 0));
}

void test_scans_once_per_interval() {
  OtaOfferThrottle t;
  TEST_ASSERT_TRUE(t.due({}, 5000));
  TEST_ASSERT_FALSE(t.due({}, 5001));
  TEST_ASSERT_FALSE(t.due({}, 5000 + OtaOfferThrottle::kIntervalMs - 1));
  TEST_ASSERT_TRUE(t.due({}, 5000 + OtaOfferThrottle::kIntervalMs));
}

void test_interval_survives_millis_wrap() {
  OtaOfferThrottle t;
  TEST_ASSERT_TRUE(t.due({}, 0xFFFFFE00u));
  TEST_ASSERT_FALSE(t.due({}, 0xFFFFFF00u));
  TEST_ASSERT_FALSE(t.due({}, 0x00000010u));
  TEST_ASSERT_TRUE(t.due({}, 0xFFFFFE00u + OtaOfferThrottle::kIntervalMs));
}

void test_long_busy_spell_never_locks_the_scan_out() {
  OtaOfferThrottle t;
  TEST_ASSERT_TRUE(t.due({}, 1000));
  TEST_ASSERT_TRUE(t.due({}, 1000 + 0x80000000u));
}

// A scan, then busy for a whole wrap: the first idle tick after scans at
// once instead of reading the old scan as 100 ms ago.
void test_scan_after_a_wrap_long_busy_spell_runs_at_once() {
  OtaOfferThrottle t;
  OtaOfferBusy busy;
  busy.quiet = true;
  const uint32_t scannedMs = 1000;
  TEST_ASSERT_TRUE(t.due({}, scannedMs));
  for (uint64_t at = scannedMs; at < scannedMs + (1ull << 32); at += 1ull << 28) {
    TEST_ASSERT_FALSE(t.due(busy, static_cast<uint32_t>(at)));
  }
  TEST_ASSERT_TRUE(t.due({}, scannedMs + 100));
}

void test_each_busy_source_blocks_the_scan() {
  OtaOfferBusy sending;   sending.firmwareSending = true;
  OtaOfferBusy receiving; receiving.firmwareReceiving = true;
  OtaOfferBusy fs;        fs.fsBusy = true;
  OtaOfferBusy quiet;     quiet.quiet = true;
  for (const OtaOfferBusy& b : {sending, receiving, fs, quiet}) {
    OtaOfferThrottle t;
    TEST_ASSERT_FALSE(t.due(b, 5000));
  }
}

void test_startup_phase_holds_offers() {
  OtaOfferThrottle t;
  OtaOfferBusy startingUp;
  startingUp.startingUp = true;
  TEST_ASSERT_FALSE(t.due(startingUp, 5000));
  TEST_ASSERT_TRUE(t.due({}, 5001));
}

void test_offer_busy_holds_until_startup_completes() {
  TEST_ASSERT_TRUE(offerBusy(/*startupComplete=*/false, false, false, false, 0, false).any());
  TEST_ASSERT_FALSE(offerBusy(/*startupComplete=*/true, false, false, false, 0, false).any());
}

void test_offer_busy_maps_each_source() {
  TEST_ASSERT_TRUE(offerBusy(true, true, false, false, 0, false).firmwareSending);
  TEST_ASSERT_TRUE(offerBusy(true, false, true, false, 0, false).firmwareReceiving);
  TEST_ASSERT_TRUE(offerBusy(true, false, false, true, 0, false).fsBusy);
  TEST_ASSERT_TRUE(offerBusy(true, false, false, false, 1, false).quiet);
}

void test_distributor_idle_hold_lets_the_scan_run() {
  TEST_ASSERT_FALSE(offerBusy(true, false, false, false, /*quietHolders=*/1,
                              /*quietIsOwnIdleHold=*/true).any());
}

void test_other_quiet_still_blocks_during_the_hold() {
  TEST_ASSERT_TRUE(offerBusy(true, false, false, false, /*quietHolders=*/2,
                             /*quietIsOwnIdleHold=*/true).quiet);
  TEST_ASSERT_TRUE(offerBusy(true, false, false, false, /*quietHolders=*/1,
                             /*quietIsOwnIdleHold=*/false).quiet);
}

void test_busy_tick_does_not_spend_the_interval() {
  OtaOfferThrottle t;
  TEST_ASSERT_TRUE(t.due({}, 5000));
  OtaOfferBusy receiving;
  receiving.firmwareReceiving = true;
  TEST_ASSERT_FALSE(t.due(receiving, 5000 + OtaOfferThrottle::kIntervalMs));
  TEST_ASSERT_TRUE(t.due({}, 5000 + OtaOfferThrottle::kIntervalMs + 1));
}

void test_fs_walk_skipped_during_firmware_hold() {
  TEST_ASSERT_FALSE(offerBusy(true, false, false, false, 1, /*quietIsOwnIdleHold=*/true)
                        .fsWalkAllowed());
  TEST_ASSERT_TRUE(offerBusy(true, false, false, false, 0, false).fsWalkAllowed());
}

void test_hold_is_inheritable_only_after_a_done_session() {
  InterSessionQuietHold hold;
  TEST_ASSERT_FALSE(hold.inheritable());
  hold.sessionEnteredQuiet();
  TEST_ASSERT_FALSE(hold.inheritable());
  TEST_ASSERT_FALSE(hold.dueToExit(100000));
  hold.sessionEnded(1000, /*done=*/true);
  TEST_ASSERT_TRUE(hold.inheritable());
  hold.offerArmed();
  TEST_ASSERT_FALSE(hold.inheritable());
  TEST_ASSERT_FALSE(hold.dueToExit(100000));
  hold.sessionEnded(2000, /*done=*/false);
  TEST_ASSERT_FALSE(hold.inheritable());
  TEST_ASSERT_TRUE(hold.dueToExit(2000 + InterSessionQuietHold::kHoldMs));
}

void test_hold_expires_across_millis_wrap() {
  InterSessionQuietHold hold;
  hold.sessionEnteredQuiet();
  const uint32_t endedAt = 0xFFFFFFFFu - InterSessionQuietHold::kHoldMs + 1;  // 2^32 - 10000
  hold.sessionEnded(endedAt, /*done=*/true);
  TEST_ASSERT_TRUE(hold.inheritable());
  TEST_ASSERT_FALSE(hold.dueToExit(endedAt + InterSessionQuietHold::kHoldMs - 1));
  TEST_ASSERT_TRUE(hold.dueToExit(endedAt + InterSessionQuietHold::kHoldMs));  // wraps to 0
  TEST_ASSERT_TRUE(hold.dueToExit(5000));
}

void test_session_without_quiet_arms_no_hold() {
  InterSessionQuietHold hold;
  hold.sessionEnded(1000, /*done=*/true);
  TEST_ASSERT_FALSE(hold.inheritable());
  TEST_ASSERT_FALSE(hold.dueToExit(1000 + InterSessionQuietHold::kHoldMs));
}

// Runs tickOtaOffers' gate against FirmwareDistributor's hold bookkeeping with
// a crowd of stale peers that never ACCEPT: every offer times out.
struct WaveSim {
  static constexpr uint32_t kAcceptTimeoutMs = 15000;  // FirmwareDistributor's

  InterSessionQuietHold hold;
  OtaOfferThrottle throttle;
  int quietHolders = 0;
  bool offerSent = false;
  uint32_t offerAtMs = 0;
  uint32_t lastSessionEndMs = 0;
  uint32_t lastSessionEndAtQuietEndMs = 0;
  uint32_t quietEndMs = 0;
  int sessionsInsideHold = 0;

  void firstSessionEnds(uint32_t nowMs, bool done) {
    quietHolders = 1;
    hold.sessionEnteredQuiet();
    hold.sessionEnded(nowMs, done);
    lastSessionEndMs = nowMs;
  }

  void step(uint32_t nowMs) {
    if (offerSent && nowMs - offerAtMs > kAcceptTimeoutMs) {
      offerSent = false;
      if (hold.held) sessionsInsideHold++;
      hold.sessionEnded(nowMs, /*done=*/false);
      lastSessionEndMs = nowMs;
    }
    if (!offerSent && hold.dueToExit(nowMs)) {
      quietHolders--;
      hold.release();
      quietEndMs = nowMs;
      lastSessionEndAtQuietEndMs = lastSessionEndMs;
    }
    const OtaOfferBusy busy =
        offerBusy(true, offerSent, false, false, quietHolders, hold.inheritable());
    if (throttle.due(busy, nowMs)) {
      offerSent = true;
      offerAtMs = nowMs;
      hold.offerArmed();
    }
  }

  void run(uint32_t fromMs, uint32_t toMs) {
    for (uint32_t t = fromMs; t <= toMs; t += 100) step(t);
  }
};

void test_failed_offers_inside_a_hold_let_quiet_end() {
  WaveSim sim;
  sim.firstSessionEnds(1000, /*done=*/true);
  sim.run(1100, 300000);
  TEST_ASSERT_EQUAL_INT(1, sim.sessionsInsideHold);
  TEST_ASSERT_NOT_EQUAL(0, sim.quietEndMs);
  TEST_ASSERT_EQUAL_INT(0, sim.quietHolders);
  const uint32_t gap = sim.quietEndMs - sim.lastSessionEndAtQuietEndMs;
  TEST_ASSERT_UINT32_WITHIN(100, InterSessionQuietHold::kHoldMs + 50, gap);
}

void test_failed_session_hold_takes_no_offer() {
  WaveSim sim;
  sim.firstSessionEnds(1000, /*done=*/false);
  sim.run(1100, 60000);
  TEST_ASSERT_EQUAL_INT(0, sim.sessionsInsideHold);
  TEST_ASSERT_UINT32_WITHIN(100, 1000 + InterSessionQuietHold::kHoldMs + 50, sim.quietEndMs);
}

MeshPeer peer(uint32_t version, bool hasMac = true) {
  MeshPeer p;
  p.firmwareVersion = version;
  p.hasMac = hasMac;
  return p;
}

void test_newer_peer_holds_offers() {
  std::vector<MeshPeer> peers{peer(0x00010001u), peer(0x00010003u)};
  TEST_ASSERT_TRUE(peerRunsNewerFirmware(peers, 0x00010002u));
}

void test_same_or_older_peers_allow_offers() {
  std::vector<MeshPeer> peers{peer(0x00010001u), peer(0x00010002u)};
  TEST_ASSERT_FALSE(peerRunsNewerFirmware(peers, 0x00010002u));
  TEST_ASSERT_FALSE(peerRunsNewerFirmware({}, 0x00010002u));
}

void test_peer_without_mac_or_version_is_ignored() {
  std::vector<MeshPeer> peers{peer(0x00010003u, /*hasMac=*/false), peer(0)};
  TEST_ASSERT_FALSE(peerRunsNewerFirmware(peers, 0x00010002u));
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_first_idle_tick_scans);
  RUN_TEST(test_scans_once_per_interval);
  RUN_TEST(test_interval_survives_millis_wrap);
  RUN_TEST(test_long_busy_spell_never_locks_the_scan_out);
  RUN_TEST(test_scan_after_a_wrap_long_busy_spell_runs_at_once);
  RUN_TEST(test_each_busy_source_blocks_the_scan);
  RUN_TEST(test_startup_phase_holds_offers);
  RUN_TEST(test_offer_busy_holds_until_startup_completes);
  RUN_TEST(test_offer_busy_maps_each_source);
  RUN_TEST(test_distributor_idle_hold_lets_the_scan_run);
  RUN_TEST(test_other_quiet_still_blocks_during_the_hold);
  RUN_TEST(test_busy_tick_does_not_spend_the_interval);
  RUN_TEST(test_fs_walk_skipped_during_firmware_hold);
  RUN_TEST(test_hold_is_inheritable_only_after_a_done_session);
  RUN_TEST(test_hold_expires_across_millis_wrap);
  RUN_TEST(test_session_without_quiet_arms_no_hold);
  RUN_TEST(test_failed_offers_inside_a_hold_let_quiet_end);
  RUN_TEST(test_failed_session_hold_takes_no_offer);
  RUN_TEST(test_newer_peer_holds_offers);
  RUN_TEST(test_same_or_older_peers_allow_offers);
  RUN_TEST(test_peer_without_mac_or_version_is_ignored);
  return UNITY_END();
}
