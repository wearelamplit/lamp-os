// Tests for NearbyLamps::getUngreetedArrivals.
//
// Uses an inline mirror (no FreeRTOS). Pins the filter contract:
// BLE-fresh + non-empty bdAddr + !acknowledged. Stale or acknowledged
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

struct NearbyLamp {
  std::string name;
  std::string bdAddr;
  uint32_t lastSeenViaBleMs = 0;
  bool acknowledged = false;
};

class NearbyLamps {
 public:
  // Seed a peer directly (mirrors what addOrUpdateFromBle would do).
  void seed(const std::string& name, const std::string& bdAddr,
            uint32_t lastSeenViaBleMs, bool acknowledged) {
    NearbyLamp e;
    e.name = name;
    e.bdAddr = bdAddr;
    e.lastSeenViaBleMs = lastSeenViaBleMs;
    e.acknowledged = acknowledged;
    store_.push_back(e);
  }

  // Production contract: BLE-reachable peers whose acknowledged flag is false.
  // BLE-reachable = lastSeenViaBleMs within maxAgeMs AND bdAddr non-empty.
  std::vector<NearbyLamp> getUngreetedArrivals(uint32_t maxAgeMs) {
    uint32_t now = millis();
    std::vector<NearbyLamp> out;
    for (const auto& e : store_) {
      if (e.bdAddr.empty()) continue;
      if (e.lastSeenViaBleMs == 0) continue;
      if ((now - e.lastSeenViaBleMs) > maxAgeMs) continue;
      if (e.acknowledged) continue;
      out.push_back(e);
    }
    return out;
  }

 private:
  std::vector<NearbyLamp> store_;
};

}  // namespace lamp

void setUp(void) { s_nowMs = 10000; }
void tearDown(void) {}

void test_fresh_unacknowledged_returned() {
  lamp::NearbyLamps lamps;
  lamps.seed("jacko", "AA:BB:CC:DD:EE:01", s_nowMs - 100, false);
  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(1, result.size());
  TEST_ASSERT_EQUAL_STRING("jacko", result[0].name.c_str());
}

void test_acknowledged_excluded() {
  lamp::NearbyLamps lamps;
  lamps.seed("jacko", "AA:BB:CC:DD:EE:01", s_nowMs - 100, true);
  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(0, result.size());
}

void test_stale_excluded() {
  lamp::NearbyLamps lamps;
  // Last seen 6000 ms ago; maxAge is 5000. Stale.
  lamps.seed("jacko", "AA:BB:CC:DD:EE:01", s_nowMs - 6000, false);
  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(0, result.size());
}

void test_empty_bdaddr_excluded() {
  lamp::NearbyLamps lamps;
  // No BLE advert yet; bdAddr empty.
  lamps.seed("phantom", "", s_nowMs - 100, false);
  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(0, result.size());
}

void test_mixed_peers_only_fresh_unacknowledged_returned() {
  lamp::NearbyLamps lamps;
  // Fresh + unacknowledged; should appear.
  lamps.seed("flora",   "AA:BB:CC:DD:EE:01", s_nowMs - 200,  false);
  // Acknowledged; excluded.
  lamps.seed("gramp",   "AA:BB:CC:DD:EE:02", s_nowMs - 200,  true);
  // Stale; excluded.
  lamps.seed("herald",  "AA:BB:CC:DD:EE:03", s_nowMs - 9000, false);
  // No BLE sighting (lastSeenViaBleMs==0); excluded.
  lamps.seed("phantom", "AA:BB:CC:DD:EE:04", 0,              false);
  // Fresh + unacknowledged; should appear.
  lamps.seed("snafu",   "AA:BB:CC:DD:EE:05", s_nowMs - 50,   false);

  auto result = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(2, result.size());

  auto hasName = [&](const std::string& n) {
    return std::any_of(result.begin(), result.end(),
                       [&](const lamp::NearbyLamp& p) { return p.name == n; });
  };
  TEST_ASSERT_TRUE(hasName("flora"));
  TEST_ASSERT_TRUE(hasName("snafu"));
}

void test_zero_last_seen_excluded() {
  lamp::NearbyLamps lamps;
  // lastSeenViaBleMs == 0 means never seen via BLE.
  lamps.seed("jacko", "AA:BB:CC:DD:EE:01", 0, false);
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
  RUN_TEST(test_empty_bdaddr_excluded);
  RUN_TEST(test_mixed_peers_only_fresh_unacknowledged_returned);
  RUN_TEST(test_zero_last_seen_excluded);
  return UNITY_END();
}
