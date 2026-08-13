// Tests for the arrival/near contract behind BehaviorContext::forEachArrival /
// forEachNearby. forEachArrival hands the single strongest ungreeted arrival
// through LampRoster::bestUngreetedArrival (an in-place under-lock scan, no
// snapshot); forEachNearby walks the getNear() snapshot in RSSI order.
//
// Inline mirror (no FreeRTOS) of that observable contract: strongest-RSSI pick,
// consumer-owned ack (ack only on cb == true), no auto-ack, once-per-arrival
// dedup, maxAge boundary, prune-and-return re-fire, near filtering. The real
// PeerView type is pinned directly for heap-safety (no std::string in the view).

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include "core/behavior_context.hpp"

// Fake millis() for deterministic time control.
static uint32_t s_nowMs = 100000;
static uint32_t millis() { return s_nowMs; }

namespace mirror {

// Mirror of the production RosterEntry fields the arrival scan reads.
struct RosterEntry {
  std::string name;
  uint8_t mac[6] = {0};
  bool hasMac = false;
  uint32_t lastSeenNearMs = 0;
  bool acknowledged = false;
  int8_t lastRssi = -127;
};

// Mirror of the roster surface forEachArrival / forEachNearby route through.
class LampRoster {
 public:
  void seed(const std::string& name, bool hasMac, uint32_t lastSeenNearMs,
            bool acknowledged, int8_t rssi) {
    RosterEntry e;
    e.name = name;
    e.hasMac = hasMac;
    e.lastSeenNearMs = lastSeenNearMs;
    e.acknowledged = acknowledged;
    e.lastRssi = rssi;
    store_.push_back(e);
  }

  void acknowledge(const std::string& name) {
    for (auto& e : store_) {
      if (e.name == name) { e.acknowledged = true; return; }
    }
  }

  // Drop a peer entirely (prune), so a later re-seed arrives unacknowledged.
  void drop(const std::string& name) {
    store_.erase(std::remove_if(store_.begin(), store_.end(),
                                [&](const RosterEntry& e) { return e.name == name; }),
                 store_.end());
  }

  // Models BehaviorContext::forEachArrival: hand the single strongest ungreeted
  // near arrival to `visit`; ack it only when visit returns true. No snapshot.
  using ArrivalVisitor = std::function<bool(const RosterEntry&)>;
  void forEachArrival(uint32_t maxAgeMs, const ArrivalVisitor& visit) {
    uint32_t now = millis();
    const RosterEntry* best = nullptr;
    for (const auto& e : store_) {
      if (!e.hasMac || e.lastSeenNearMs == 0 ||
          (now - e.lastSeenNearMs) > maxAgeMs || e.acknowledged) {
        continue;
      }
      if (!best || e.lastRssi > best->lastRssi) best = &e;
    }
    if (best && visit(*best)) acknowledge(best->name);
  }

  // Models BehaviorContext::forEachNearby: walk near peers strongest-first,
  // stop on true, never ack.
  using NearVisitor = std::function<bool(const RosterEntry&)>;
  void forEachNear(uint32_t maxAgeMs, const NearVisitor& visit) {
    uint32_t now = millis();
    std::vector<RosterEntry> snap = store_;
    snap.erase(std::remove_if(snap.begin(), snap.end(),
                              [&](const RosterEntry& e) {
                                return e.lastSeenNearMs == 0 ||
                                       (now - e.lastSeenNearMs) > maxAgeMs;
                              }),
               snap.end());
    std::stable_sort(snap.begin(), snap.end(),
                     [](const RosterEntry& a, const RosterEntry& b) {
                       return a.lastRssi > b.lastRssi;
                     });
    for (const auto& e : snap) {
      if (visit(e)) return;
    }
  }

 private:
  std::vector<RosterEntry> store_;
};

}  // namespace mirror

void setUp(void) { s_nowMs = 100000; }
void tearDown(void) {}

// --- PeerView is heap-safe: no std::string in the view -------------------
void test_peer_view_is_trivially_copyable() {
  // A std::string member would make PeerView non-trivially-copyable and would
  // heap-allocate the 17-char lampId. char[]/pointers keep it off the heap.
  TEST_ASSERT_TRUE(std::is_trivially_copyable<lamp::PeerView>::value);
}

// --- forEachArrival: consumer-owned ack ----------------------------------
void test_fires_once_then_dedups_after_ack() {
  mirror::LampRoster roster;
  roster.seed("jacko", true, s_nowMs - 100, false, -50);
  int hits = 0;
  auto greet = [&](const mirror::RosterEntry&) { hits++; return true; };
  roster.forEachArrival(5000, greet);
  roster.forEachArrival(5000, greet);
  TEST_ASSERT_EQUAL_INT(1, hits);  // acked on first true, not re-fired
}

void test_false_return_leaves_peer_retriable() {
  mirror::LampRoster roster;
  roster.seed("jacko", true, s_nowMs - 100, false, -50);
  int hits = 0;
  auto busy = [&](const mirror::RosterEntry&) { hits++; return false; };
  roster.forEachArrival(5000, busy);
  roster.forEachArrival(5000, busy);
  TEST_ASSERT_EQUAL_INT(2, hits);  // never acked, still an arrival each tick
}

void test_no_auto_ack_on_visitation() {
  // Visiting without ever returning true must not acknowledge; the peer
  // remains a retry token indefinitely.
  mirror::LampRoster roster;
  roster.seed("jacko", true, s_nowMs - 100, false, -50);
  for (int i = 0; i < 5; i++) {
    roster.forEachArrival(5000, [](const mirror::RosterEntry&) { return false; });
  }
  int hits = 0;
  roster.forEachArrival(5000, [&](const mirror::RosterEntry&) { hits++; return false; });
  TEST_ASSERT_EQUAL_INT(1, hits);
}

// --- Order: strongest RSSI wins, one arrival per call --------------------
void test_strongest_rssi_first_and_stops_on_true() {
  mirror::LampRoster roster;
  roster.seed("far",   true, s_nowMs - 100, false, -80);
  roster.seed("close", true, s_nowMs - 100, false, -40);
  roster.seed("mid",   true, s_nowMs - 100, false, -60);
  std::vector<std::string> visited;
  roster.forEachArrival(5000, [&](const mirror::RosterEntry& e) {
    visited.push_back(e.name);
    return true;  // greet the closest and stop
  });
  TEST_ASSERT_EQUAL_UINT(1, visited.size());
  TEST_ASSERT_EQUAL_STRING("close", visited[0].c_str());
}

void test_false_offers_only_strongest() {
  // Single-best pick: a false return re-offers the strongest next call, it does
  // not walk the rest of the roster (no snapshot, no per-tick sort).
  mirror::LampRoster roster;
  roster.seed("far",   true, s_nowMs - 100, false, -80);
  roster.seed("close", true, s_nowMs - 100, false, -40);
  roster.seed("mid",   true, s_nowMs - 100, false, -60);
  std::vector<std::string> visited;
  roster.forEachArrival(5000, [&](const mirror::RosterEntry& e) {
    visited.push_back(e.name);
    return false;
  });
  TEST_ASSERT_EQUAL_UINT(1, visited.size());
  TEST_ASSERT_EQUAL_STRING("close", visited[0].c_str());
}

// --- maxAge boundary (inclusive at exactly maxAge) -----------------------
void test_maxage_boundary_inclusive() {
  mirror::LampRoster roster;
  roster.seed("edge", true, s_nowMs - 5000, false, -50);  // exactly maxAge old
  int hits = 0;
  roster.forEachArrival(5000, [&](const mirror::RosterEntry&) { hits++; return false; });
  TEST_ASSERT_EQUAL_INT(1, hits);
}

void test_maxage_boundary_excludes_older() {
  mirror::LampRoster roster;
  roster.seed("stale", true, s_nowMs - 5001, false, -50);  // one ms too old
  int hits = 0;
  roster.forEachArrival(5000, [&](const mirror::RosterEntry&) { hits++; return false; });
  TEST_ASSERT_EQUAL_INT(0, hits);
}

// --- Pruned-and-returned peer re-fires -----------------------------------
void test_pruned_and_returned_refires() {
  mirror::LampRoster roster;
  roster.seed("wanderer", true, s_nowMs - 100, false, -50);
  roster.forEachArrival(5000, [](const mirror::RosterEntry&) { return true; });  // ack
  int firstPass = 0;
  roster.forEachArrival(5000, [&](const mirror::RosterEntry&) { firstPass++; return false; });
  TEST_ASSERT_EQUAL_INT(0, firstPass);  // acked, silent

  roster.drop("wanderer");                                   // prune
  roster.seed("wanderer", true, s_nowMs - 10, false, -50);   // returns fresh
  int refire = 0;
  roster.forEachArrival(5000, [&](const mirror::RosterEntry&) { refire++; return false; });
  TEST_ASSERT_EQUAL_INT(1, refire);
}

// --- forEachNearby filters + orders --------------------------------------
void test_near_visits_only_recent_in_rssi_order() {
  mirror::LampRoster roster;
  roster.seed("near_far",  true, s_nowMs - 100,   false, -70);
  roster.seed("stale",     true, s_nowMs - 99999, false, -30);  // not near
  roster.seed("near_close",true, s_nowMs - 100,   false, -45);
  roster.seed("nonear",    true, 0,               false, -20);  // never near
  std::vector<std::string> visited;
  roster.forEachNear(5000, [&](const mirror::RosterEntry& e) {
    visited.push_back(e.name);
    return false;
  });
  TEST_ASSERT_EQUAL_UINT(2, visited.size());
  TEST_ASSERT_EQUAL_STRING("near_close", visited[0].c_str());
  TEST_ASSERT_EQUAL_STRING("near_far",   visited[1].c_str());
}

void test_near_stops_on_true() {
  mirror::LampRoster roster;
  roster.seed("a", true, s_nowMs - 100, false, -40);
  roster.seed("b", true, s_nowMs - 100, false, -60);
  int hits = 0;
  roster.forEachNear(5000, [&](const mirror::RosterEntry&) { hits++; return true; });
  TEST_ASSERT_EQUAL_INT(1, hits);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_peer_view_is_trivially_copyable);
  RUN_TEST(test_fires_once_then_dedups_after_ack);
  RUN_TEST(test_false_return_leaves_peer_retriable);
  RUN_TEST(test_no_auto_ack_on_visitation);
  RUN_TEST(test_strongest_rssi_first_and_stops_on_true);
  RUN_TEST(test_false_offers_only_strongest);
  RUN_TEST(test_maxage_boundary_inclusive);
  RUN_TEST(test_maxage_boundary_excludes_older);
  RUN_TEST(test_pruned_and_returned_refires);
  RUN_TEST(test_near_visits_only_recent_in_rssi_order);
  RUN_TEST(test_near_stops_on_true);
  return UNITY_END();
}
