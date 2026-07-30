// Real-code pin for PeerView::from + the ack-after-scan greet path. Includes
// the shipped LampRoster and peer_view.cpp so the RosterEntry → PeerView
// conversion and BehaviorContext::forEachArrival's `bestUngreetedArrival` +
// `acknowledge(out.mac)` sequence are exercised against production, not a mirror.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "components/network/mesh/lamp_roster.cpp"
#include "core/peer_view.cpp"

using namespace lamp;

namespace {
auto acceptAll = [](const RosterEntry&) { return true; };
}  // namespace

void setUp() { set_mock_millis(100000); }
void tearDown() {}

// PeerView::from copies every field the greeting seam reads, and lampId is the
// canonical colon-hex of the BLE-recovered mesh mac.
void test_peerview_from_populates_all_fields() {
  LampRoster r;
  const Color base(10, 20, 30, 40);
  const Color shade(1, 2, 3, 4);
  r.addOrUpdateFromBle("flora", "AA:BB:CC:DD:EE:FF", base, shade, -50);

  RosterEntry out;
  TEST_ASSERT_TRUE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
  const PeerView v = PeerView::from(out);
  TEST_ASSERT_EQUAL_STRING("flora", v.name);
  TEST_ASSERT_TRUE(v.hasMac);
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FD", v.lampId);  // scanned addr - 2
  TEST_ASSERT_TRUE(v.baseColor == base);
  TEST_ASSERT_TRUE(v.shadeColor == shade);
  TEST_ASSERT_EQUAL_INT8(-50, v.rssi);
  const uint8_t wantMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFD};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wantMac, v.mac, 6);
}

// forEachArrival acks by MAC after a greeting (cb == true), so the peer no
// longer arrives; a rename can't dodge the ack.
void test_greet_acks_by_mac() {
  LampRoster r;
  r.addOrUpdateFromBle("flora", "AA:BB:CC:DD:EE:FF", Color(), Color(), -50);

  RosterEntry out;
  TEST_ASSERT_TRUE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
  r.acknowledge(out.mac);  // the ack forEachArrival folds into cb == true

  RosterEntry second;
  TEST_ASSERT_FALSE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, second));
}

// A scan without an ack (cb == false) never acknowledges: the peer stays a
// retry token across ticks.
void test_scan_without_ack_leaves_arrival() {
  LampRoster r;
  r.addOrUpdateFromBle("flora", "AA:BB:CC:DD:EE:FF", Color(), Color(), -50);

  RosterEntry out;
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
  }
  TEST_ASSERT_TRUE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
}

// Strongest RSSI wins: the pick is the closest arrival.
void test_strongest_rssi_first() {
  LampRoster r;
  r.addOrUpdateFromBle("far",   "AA:BB:CC:DD:EE:03", Color(), Color(), -80);
  r.addOrUpdateFromBle("close", "AA:BB:CC:DD:EE:05", Color(), Color(), -40);
  r.addOrUpdateFromBle("mid",   "AA:BB:CC:DD:EE:07", Color(), Color(), -60);

  RosterEntry out;
  TEST_ASSERT_TRUE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
  TEST_ASSERT_EQUAL_STRING("close", PeerView::from(out).name);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_peerview_from_populates_all_fields);
  RUN_TEST(test_greet_acks_by_mac);
  RUN_TEST(test_scan_without_ack_leaves_arrival);
  RUN_TEST(test_strongest_rssi_first);
  return UNITY_END();
}
