// Native-host tests for the Social/Network view duty-cycled scan.
//
// Two halves:
//  1. SocialScanGate (the real production state machine, header-only): a
//     window opens after the gap when idle, the app-idle guard skips a
//     window, the window closes after its length, and the gate stops on
//     flag-clear/disconnect (active=false) and on the safety timeout.
//  2. The sighting pipeline the gate re-enables: a simulated BLE-only advert
//     via LampRoster::addOrUpdateFromBle lands in the roster with the
//     BLE-near fields buildNearbyJson emits (viaBle path). The nearby-JSON
//     serialize itself is covered by test_nearby_rev / test_ble_nearby_lampid.

#include <unity.h>

#include <cstdint>
#include <string>

#include "components/network/ble/social_scan.hpp"
#include "components/network/mesh/lamp_roster.cpp"

using ble_control::SocialScanAction;
using ble_control::SocialScanGate;
using ble_control::SocialScanTuning;

void setUp(void) {}
void tearDown(void) {}

namespace {
constexpr SocialScanTuning kT{};  // 30ms window / 300ms gap / 150ms idle / 600s cap
constexpr uint32_t kIdle = 0;     // no recent write when now is far past idleGuard
}  // namespace

// Armed + idle + past the gap => a window opens, then closes after windowMs.
void test_window_opens_after_gap_and_closes_after_window() {
  SocialScanGate g;
  uint32_t now = 100000;  // large so (now - lastWrite) exceeds idleGuardMs
  g.arm(now);

  // Immediately after arm the first gap hasn't elapsed.
  TEST_ASSERT_EQUAL(SocialScanAction::None,
                    g.step(now, true, kIdle, kT));
  TEST_ASSERT_FALSE(g.windowOpen());

  // After the gap, the window opens.
  now += kT.gapMs;
  TEST_ASSERT_EQUAL(SocialScanAction::OpenWindow,
                    g.step(now, true, kIdle, kT));
  TEST_ASSERT_TRUE(g.windowOpen());

  // Mid-window: nothing changes.
  now += kT.windowMs - 1;
  TEST_ASSERT_EQUAL(SocialScanAction::None,
                    g.step(now, true, kIdle, kT));
  TEST_ASSERT_TRUE(g.windowOpen());

  // Window elapsed: it closes.
  now += 1;
  TEST_ASSERT_EQUAL(SocialScanAction::CloseWindow,
                    g.step(now, true, kIdle, kT));
  TEST_ASSERT_FALSE(g.windowOpen());
}

// A BLE write inside idleGuardMs skips the window; once idle, it opens.
void test_idle_guard_skips_window_during_recent_write() {
  SocialScanGate g;
  uint32_t now = 100000;
  g.arm(now);
  now += kT.gapMs;  // gap satisfied

  const uint32_t recentWrite = now - (kT.idleGuardMs - 1);
  TEST_ASSERT_EQUAL(SocialScanAction::None,
                    g.step(now, true, recentWrite, kT));
  TEST_ASSERT_FALSE(g.windowOpen());

  // Advance past the idle guard relative to that write: window opens.
  now = recentWrite + kT.idleGuardMs;
  TEST_ASSERT_EQUAL(SocialScanAction::OpenWindow,
                    g.step(now, true, recentWrite, kT));
  TEST_ASSERT_TRUE(g.windowOpen());
}

// Flag clear / disconnect (active=false) with a window open => it closes.
void test_close_on_flag_clear_while_window_open() {
  SocialScanGate g;
  uint32_t now = 100000;
  g.arm(now);
  now += kT.gapMs;
  TEST_ASSERT_EQUAL(SocialScanAction::OpenWindow,
                    g.step(now, true, kIdle, kT));

  now += 5;
  TEST_ASSERT_EQUAL(SocialScanAction::CloseWindow,
                    g.step(now, /*active=*/false, kIdle, kT));
  TEST_ASSERT_FALSE(g.windowOpen());

  // Inactive stays quiet.
  now += kT.gapMs * 4;
  TEST_ASSERT_EQUAL(SocialScanAction::None,
                    g.step(now, false, kIdle, kT));
}

// Past the safety cap, no new window opens even while armed + idle.
void test_safety_timeout_stops_new_windows() {
  SocialScanGate g;
  uint32_t now = 100000;
  g.arm(now);
  now += kT.maxSessionMs + 1;
  TEST_ASSERT_EQUAL(SocialScanAction::None,
                    g.step(now, true, kIdle, kT));
  TEST_ASSERT_FALSE(g.windowOpen());
}

// The pipeline the open window feeds: a BLE-only advert enters the roster
// with the near/viaBle fields the nearby JSON reads.
void test_ble_only_sighting_enters_roster() {
  set_mock_millis(50000);  // lastSeenNearMs stamps millis(); keep it nonzero
  lamp::LampRoster roster;
  const lamp::Color base(10, 20, 30, 0);
  const lamp::Color shade(40, 50, 60, 0);
  roster.addOrUpdateFromBle("legacy-lamp", "c4:dd:57:eb:64:62", base, shade,
                            /*rssi=*/-70);

  auto all = roster.getAll();
  TEST_ASSERT_EQUAL_UINT32(1, all.size());
  const auto& e = all[0];
  TEST_ASSERT_EQUAL_STRING("legacy-lamp", e.name);
  TEST_ASSERT_NOT_EQUAL(0, e.lastSeenNearMs);  // viaBle flag in buildNearbyJson
  TEST_ASSERT_EQUAL_INT8(-70, e.lastRssi);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_window_opens_after_gap_and_closes_after_window);
  RUN_TEST(test_idle_guard_skips_window_during_recent_write);
  RUN_TEST(test_close_on_flag_clear_while_window_open);
  RUN_TEST(test_safety_timeout_stops_new_windows);
  RUN_TEST(test_ble_only_sighting_enters_roster);
  return UNITY_END();
}
