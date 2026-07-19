// Tests for LampRoster::getUngreetedArrivals.
//
// Uses an inline mirror (no FreeRTOS). Pins the filter contract:
// near-fresh + hasMac + !acknowledged. Stale or acknowledged
// peers must be excluded.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Fake millis() for deterministic time control.
static uint32_t s_nowMs = 10000;
static uint32_t millis() { return s_nowMs; }

namespace lamp {

struct RosterEntry {
  std::string name;
  bool hasMac = false;
  uint32_t lastSeenNearMs = 0;
  bool acknowledged = false;
};

class LampRoster {
 public:
  // Seed a peer directly (mirrors what addOrUpdateFromBle would do).
  void seed(const std::string& name, bool hasMac,
            uint32_t lastSeenNearMs, bool acknowledged) {
    RosterEntry e;
    e.name = name;
    e.hasMac = hasMac;
    e.lastSeenNearMs = lastSeenNearMs;
    e.acknowledged = acknowledged;
    store_.push_back(e);
  }

  // Production contract: near peers whose acknowledged flag is false.
  // Near = lastSeenNearMs within maxAgeMs AND hasMac.
  std::vector<RosterEntry> getUngreetedArrivals(uint32_t maxAgeMs) {
    uint32_t now = millis();
    std::vector<RosterEntry> out;
    for (const auto& e : store_) {
      if (!e.hasMac) continue;
      if (e.lastSeenNearMs == 0) continue;
      if ((now - e.lastSeenNearMs) > maxAgeMs) continue;
      if (e.acknowledged) continue;
      out.push_back(e);
    }
    return out;
  }

 private:
  std::vector<RosterEntry> store_;
};

}  // namespace lamp

void setUp(void) { s_nowMs = 10000; }
void tearDown(void) {}

void test_fresh_unacknowledged_returned() {
  lamp::LampRoster lamps;
  lamps.seed("jacko", true, s_nowMs - 100, false);
  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(1, result.size());
  TEST_ASSERT_EQUAL_STRING("jacko", result[0].name.c_str());
}

void test_acknowledged_excluded() {
  lamp::LampRoster lamps;
  lamps.seed("jacko", true, s_nowMs - 100, true);
  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(0, result.size());
}

void test_stale_excluded() {
  lamp::LampRoster lamps;
  // Last seen 6000 ms ago; maxAge is 5000. Stale.
  lamps.seed("jacko", true, s_nowMs - 6000, false);
  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(0, result.size());
}

void test_no_mac_excluded() {
  lamp::LampRoster lamps;
  // No sighting has yielded a mac yet.
  lamps.seed("phantom", false, s_nowMs - 100, false);
  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(0, result.size());
}

void test_mixed_peers_only_fresh_unacknowledged_returned() {
  lamp::LampRoster lamps;
  // Fresh + unacknowledged; should appear.
  lamps.seed("flora",   true, s_nowMs - 200,  false);
  // Acknowledged; excluded.
  lamps.seed("gramp",   true, s_nowMs - 200,  true);
  // Stale; excluded.
  lamps.seed("herald",  true, s_nowMs - 9000, false);
  // No near sighting (lastSeenNearMs==0); excluded.
  lamps.seed("phantom", true, 0,              false);
  // Fresh + unacknowledged; should appear.
  lamps.seed("snafu",   true, s_nowMs - 50,   false);

  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(2, result.size());

  auto hasName = [&](const std::string& n) {
    return std::any_of(result.begin(), result.end(),
                       [&](const lamp::RosterEntry& p) { return p.name == n; });
  };
  TEST_ASSERT_TRUE(hasName("flora"));
  TEST_ASSERT_TRUE(hasName("snafu"));
}

void test_zero_last_seen_excluded() {
  lamp::LampRoster lamps;
  // lastSeenNearMs == 0 means never seen via BLE.
  lamps.seed("jacko", true, 0, false);
  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(0, result.size());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_fresh_unacknowledged_returned);
  RUN_TEST(test_acknowledged_excluded);
  RUN_TEST(test_stale_excluded);
  RUN_TEST(test_no_mac_excluded);
  RUN_TEST(test_mixed_peers_only_fresh_unacknowledged_returned);
  RUN_TEST(test_zero_last_seen_excluded);
  return UNITY_END();
}
