#include <unity.h>
#include <string>
#include <vector>
#include <algorithm>

#include "components/network/mesh/proximity.hpp"

// Native-test seam: include the real .cpp so the RSSI gate is exercised
// against LampRoster::addOrUpdateFromBle / bestUngreetedArrival, not a
// hand-rolled mirror.
#include "components/network/mesh/lamp_roster.cpp"

using namespace lamp;

void test_near_rssi_gate() {
  TEST_ASSERT_TRUE(isNearRssi(-50, kNearRssiEspNow));   // strong, clears
  TEST_ASSERT_FALSE(isNearRssi(-90, kNearRssiEspNow));  // weak, below
  TEST_ASSERT_FALSE(isNearRssi(-127, kNearRssiEspNow)); // unknown sentinel never near
}

void test_is_near_now() {
  TEST_ASSERT_TRUE(isNearNow(100000, 150000, 240000));   // recent
  TEST_ASSERT_FALSE(isNearNow(0, 150000, 240000));       // never seen
  TEST_ASSERT_FALSE(isNearNow(100000, 500000, 240000));  // stale
  TEST_ASSERT_TRUE(isNearNow(100000, 340000, 240000));   // elapsed == maxAgeMs, boundary inclusive
}

void test_direct_vs_relayed_hello() {
  const uint8_t a[6] = {1,2,3,4,5,6};
  const uint8_t b[6] = {1,2,3,4,5,6};
  const uint8_t c[6] = {9,9,9,9,9,9};
  TEST_ASSERT_TRUE(isDirectHello(a, b));   // frame source == originator: direct
  TEST_ASSERT_FALSE(isDirectHello(c, b));  // relayed: frame source != originator
}

// Wisp adoption is single-hop: handleRecv feeds a wisp frame to the display
// slot only when the frame transmitter is the originating wisp. The same
// isDirectHello predicate gates all four wisp msg types (HELLO/CLAIM/
// PALETTE/PAINT); once posted to a pending slot only sourceMac survives, so
// the direct test can only happen at reception.
void test_wisp_adopt_only_direct() {
  const uint8_t wispMac[6]  = {0xAA,0xBB,0xCC,0x01,0x02,0x03};
  const uint8_t relayMac[6] = {0x11,0x22,0x33,0x44,0x55,0x66};
  TEST_ASSERT_TRUE(isDirectHello(wispMac, wispMac));   // heard directly: adopt
  TEST_ASSERT_FALSE(isDirectHello(relayMac, wispMac)); // relayed copy: reject
}

// addOrUpdateFromEspNow sets the mesh timestamp; markNear sets the near
// signal, the separate call the mesh_link RSSI gate makes.
void test_markNear_sets_near() {
  set_mock_millis(100000);
  LampRoster r;
  uint8_t mac[6] = {0xB8, 0xD6, 0x1A, 0x44, 0xA3, 0x5C};
  auto acceptAll = [](const RosterEntry&) { return true; };
  RosterEntry out;
  r.addOrUpdateFromEspNow("meshpeer", mac, Color(), Color());
  TEST_ASSERT_FALSE(r.bestUngreetedArrival(240000, g_mock_millis, acceptAll, out));
  r.markNear("meshpeer");
  TEST_ASSERT_TRUE(r.bestUngreetedArrival(240000, g_mock_millis, acceptAll, out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_near_rssi_gate);
  RUN_TEST(test_is_near_now);
  RUN_TEST(test_direct_vs_relayed_hello);
  RUN_TEST(test_wisp_adopt_only_direct);
  RUN_TEST(test_markNear_sets_near);
  return UNITY_END();
}
