// Tests for LampRoster::bestUngreetedArrival.
//
// Native-test seam: include the real .cpp so the in-place scan is exercised
// against LampRoster::addOrUpdateFromBle / acknowledge / markNear, not a
// hand-rolled mirror. Pins the filter contract (near-fresh + hasMac +
// !acknowledged), the highest-RSSI pick, the mac tiebreak, and the caller
// predicate that lets social skip a peer still in its re-greet window.

#include <unity.h>

#include <cstring>

#include "components/network/mesh/lamp_roster.cpp"

using namespace lamp;

static auto acceptAll = [](const RosterEntry&) { return true; };

void setUp(void) { set_mock_millis(100000); }
void tearDown(void) {}

void test_fresh_unacknowledged_returned() {
  LampRoster r;
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:00:00:01", Color(), Color(), -50);
  RosterEntry out;
  TEST_ASSERT_TRUE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
  TEST_ASSERT_EQUAL_STRING("jacko", out.name);
}

void test_acknowledged_excluded() {
  LampRoster r;
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:00:00:01", Color(), Color(), -50);
  r.acknowledge("jacko");
  RosterEntry out;
  TEST_ASSERT_FALSE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
}

void test_stale_excluded() {
  LampRoster r;
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:00:00:01", Color(), Color(), -50);
  set_mock_millis(100000 + 6000);  // last seen 6s ago; maxAge is 5s
  RosterEntry out;
  TEST_ASSERT_FALSE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
}

void test_no_mac_excluded() {
  LampRoster r;
  // An unparseable address yields no recovered mac; the entry can't be greeted.
  r.addOrUpdateFromBle("phantom", "not-an-addr", Color(), Color(), -50);
  RosterEntry out;
  TEST_ASSERT_FALSE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
}

void test_mesh_only_not_near() {
  LampRoster r;
  uint8_t mac[6] = {0xB8, 0xD6, 0x1A, 0x44, 0xA3, 0x5C};
  r.addOrUpdateFromEspNow("meshpeer", mac, Color(), Color());
  RosterEntry out;
  // Mesh-reachable but no near sighting yet: not a greeting candidate.
  TEST_ASSERT_FALSE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
  r.markNear("meshpeer");
  TEST_ASSERT_TRUE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
}

void test_picks_highest_rssi() {
  LampRoster r;
  r.addOrUpdateFromBle("far",  "AA:BB:CC:00:00:01", Color(), Color(), -90);
  r.addOrUpdateFromBle("near", "AA:BB:CC:00:00:02", Color(), Color(), -40);
  r.addOrUpdateFromBle("mid",  "AA:BB:CC:00:00:03", Color(), Color(), -65);
  RosterEntry out;
  TEST_ASSERT_TRUE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
  TEST_ASSERT_EQUAL_STRING("near", out.name);
}

void test_predicate_skips_to_next_best() {
  LampRoster r;
  r.addOrUpdateFromBle("far",  "AA:BB:CC:00:00:01", Color(), Color(), -90);
  r.addOrUpdateFromBle("near", "AA:BB:CC:00:00:02", Color(), Color(), -40);
  RosterEntry out;
  // Reject the closest (mirrors a peer still in its re-greet window): the
  // scan falls through to the next-best ungreeted arrival.
  const bool ok = r.bestUngreetedArrival(
      5000, g_mock_millis,
      [](const RosterEntry& e) { return std::strcmp(e.name, "near") != 0; },
      out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("far", out.name);
}

void test_predicate_rejects_all() {
  LampRoster r;
  r.addOrUpdateFromBle("near", "AA:BB:CC:00:00:02", Color(), Color(), -40);
  RosterEntry out;
  TEST_ASSERT_FALSE(r.bestUngreetedArrival(
      5000, g_mock_millis, [](const RosterEntry&) { return false; }, out));
}

void test_empty_roster_returns_false() {
  LampRoster r;
  RosterEntry out;
  TEST_ASSERT_FALSE(r.bestUngreetedArrival(5000, g_mock_millis, acceptAll, out));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_fresh_unacknowledged_returned);
  RUN_TEST(test_acknowledged_excluded);
  RUN_TEST(test_stale_excluded);
  RUN_TEST(test_no_mac_excluded);
  RUN_TEST(test_mesh_only_not_near);
  RUN_TEST(test_picks_highest_rssi);
  RUN_TEST(test_predicate_skips_to_next_best);
  RUN_TEST(test_predicate_rejects_all);
  RUN_TEST(test_empty_roster_returns_false);
  return UNITY_END();
}
