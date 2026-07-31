// Native-host pin for ArrivalNotifier against the real LampRoster + PeerView.
//
// Fire-once-per-new-near-peer, re-arm on the near-window edge (departure, NOT
// roster prune), no coupling to the greeting `acknowledged` flag, a heap-free
// capacity-bounded fired set, and throttle honoured. The notifier's fired set
// and re-arm are the unique logic; the roster + PeerView::from are exercised
// as shipped (test_lamp_roster_rssi convention).

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/arrival_notifier.cpp"
#include "components/network/mesh/lamp_roster.cpp"
#include "core/peer_view.cpp"

using namespace lamp;

namespace {

const Color kNoColor = Color();

struct Sink {
  int fires = 0;
  std::vector<std::string> names;
  std::vector<std::string> lampIds;
  bool lastHadMac = false;
  void operator()(const PeerView& p) {
    fires++;
    names.emplace_back(p.name);
    lampIds.emplace_back(p.lampId);
    lastHadMac = p.hasMac;
  }
};

// Seed a peer as near at the current mock time. addOrUpdateFromBle stamps
// lastSeenNearMs = millis() and recovers the mesh mac as (scanned addr - 2).
void seedNear(LampRoster& r, const char* name, const char* bleAddr, int8_t rssi) {
  r.addOrUpdateFromBle(name, bleAddr, kNoColor, kNoColor, rssi);
}

}  // namespace

void setUp() { set_mock_millis(100000); }
void tearDown() {}

void test_fires_once_per_new_near_peer() {
  LampRoster roster;
  ArrivalNotifier n;
  Sink sink;
  n.onArrival([&](const PeerView& p) { sink(p); });

  set_mock_millis(100000);
  seedNear(roster, "jacko", "AA:BB:CC:DD:EE:F0", -50);

  set_mock_millis(100010);
  n.tick(100010, roster, /*nearWindowMs=*/240000);
  TEST_ASSERT_EQUAL_INT(1, sink.fires);
  TEST_ASSERT_EQUAL_STRING("jacko", sink.names[0].c_str());
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:EE", sink.lampIds[0].c_str());  // scanned - 2
  TEST_ASSERT_TRUE(sink.lastHadMac);

  // Still near, past the throttle: dedups, no second fire.
  set_mock_millis(101000);
  n.tick(101000, roster, 240000);
  TEST_ASSERT_EQUAL_INT(1, sink.fires);
  TEST_ASSERT_EQUAL_size_t(1, n.firedCount());
}

void test_no_fire_within_throttle_gate() {
  LampRoster roster;
  ArrivalNotifier n;
  Sink sink;
  n.onArrival([&](const PeerView& p) { sink(p); });

  set_mock_millis(100000);
  seedNear(roster, "jacko", "AA:BB:CC:DD:EE:F0", -50);
  n.tick(100000, roster, 240000);
  TEST_ASSERT_EQUAL_INT(1, sink.fires);

  set_mock_millis(100100);
  seedNear(roster, "bob", "AA:BB:CC:DD:EE:B0", -55);
  n.tick(100100, roster, 240000);  // 100 ms < kThrottleMs: no-op
  TEST_ASSERT_EQUAL_INT(1, sink.fires);

  set_mock_millis(100800);
  n.tick(100800, roster, 240000);  // gate elapsed: bob fires
  TEST_ASSERT_EQUAL_INT(2, sink.fires);
  TEST_ASSERT_EQUAL_STRING("bob", sink.names[1].c_str());
}

// Departure = left the near window, NOT a roster prune. The entry stays
// resident the whole time; only lastSeenNearMs ages past nearWindow.
void test_rearms_on_near_window_edge_not_prune() {
  LampRoster roster;
  ArrivalNotifier n;
  Sink sink;
  n.onArrival([&](const PeerView& p) { sink(p); });
  const uint32_t nearWindow = 20000;

  set_mock_millis(100000);
  seedNear(roster, "wanderer", "AA:BB:CC:DD:EE:F0", -50);
  n.tick(100010, roster, nearWindow);
  TEST_ASSERT_EQUAL_INT(1, sink.fires);
  TEST_ASSERT_EQUAL_size_t(1, n.firedCount());

  // Age past the near window without ever pruning: the entry is still there.
  const uint32_t departed = 100000 + nearWindow + 5000;
  set_mock_millis(departed);  // getNear reads millis() internally; keep it in step
  n.tick(departed, roster, nearWindow);
  TEST_ASSERT_EQUAL_INT(1, sink.fires);        // no re-fire on departure
  TEST_ASSERT_EQUAL_size_t(0, n.firedCount());  // re-armed
  TEST_ASSERT_EQUAL_size_t(1, roster.getAll().size());  // never pruned

  // Returns fresh: fires again.
  set_mock_millis(departed + 1000);
  seedNear(roster, "wanderer", "AA:BB:CC:DD:EE:F0", -50);
  n.tick(departed + 1000, roster, nearWindow);
  TEST_ASSERT_EQUAL_INT(2, sink.fires);
  TEST_ASSERT_EQUAL_size_t(1, roster.getAll().size());
}

void test_no_greeting_ack_coupling() {
  LampRoster roster;
  ArrivalNotifier n;
  Sink sink;
  n.onArrival([&](const PeerView& p) { sink(p); });

  set_mock_millis(100000);
  seedNear(roster, "jacko", "AA:BB:CC:DD:EE:F0", -50);
  n.tick(100010, roster, 240000);
  TEST_ASSERT_EQUAL_INT(1, sink.fires);

  // Firing an arrival must not acknowledge it: SocialBehavior's arrival scan
  // still sees the peer.
  RosterEntry ungreeted;
  TEST_ASSERT_TRUE(roster.bestUngreetedArrival(
      5000, g_mock_millis, [](const RosterEntry&) { return true; }, ungreeted));
  TEST_ASSERT_EQUAL_STRING("jacko", ungreeted.name);
}

void test_fired_set_heap_safe_and_capacity_bounded() {
  LampRoster roster;
  ArrivalNotifier n;
  Sink sink;
  n.onArrival([&](const PeerView& p) { sink(p); });

  set_mock_millis(100000);
  for (int i = 0; i < 20; ++i) {
    char addr[18];
    std::snprintf(addr, sizeof(addr), "AA:BB:CC:DD:%02X:F0", i);
    seedNear(roster, "peer", addr, static_cast<int8_t>(-40 - i));
  }
  n.tick(100010, roster, 240000);

  TEST_ASSERT_EQUAL_size_t(ArrivalNotifier::kMaxFired, n.firedCount());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ArrivalNotifier::kMaxFired), sink.fires);
}

void test_zero_observers_is_noop() {
  LampRoster roster;
  ArrivalNotifier n;  // no onArrival

  set_mock_millis(100000);
  seedNear(roster, "jacko", "AA:BB:CC:DD:EE:F0", -50);
  n.tick(100010, roster, 240000);  // returns before touching the roster
  TEST_ASSERT_EQUAL_size_t(0, n.firedCount());
}

void test_only_fresh_arrivals_fire() {
  LampRoster roster;
  ArrivalNotifier n;
  Sink sink;
  n.onArrival([&](const PeerView& p) { sink(p); });

  // Seen 10 s ago: within the 240 s near window but past the 5 s arrival
  // window, so it's "been around", not a fresh arrival.
  set_mock_millis(100000);
  seedNear(roster, "stale", "AA:BB:CC:DD:EE:F0", -50);
  n.tick(110000, roster, 240000);
  TEST_ASSERT_EQUAL_INT(0, sink.fires);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fires_once_per_new_near_peer);
  RUN_TEST(test_no_fire_within_throttle_gate);
  RUN_TEST(test_rearms_on_near_window_edge_not_prune);
  RUN_TEST(test_no_greeting_ack_coupling);
  RUN_TEST(test_fired_set_heap_safe_and_capacity_bounded);
  RUN_TEST(test_zero_observers_is_noop);
  RUN_TEST(test_only_fresh_arrivals_fire);
  return UNITY_END();
}
