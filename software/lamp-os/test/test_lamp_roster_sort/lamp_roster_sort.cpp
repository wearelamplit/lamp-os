// Tests for the getNear / getUngreetedArrivals sort order.
//
// The production path uses std::sort (in-place introsort, no merge-buffer
// heap allocation) so it can't throw bad_alloc on the fragmented lamp heap,
// where std::stable_sort's temp buffer previously crashed a lamp mid-BLE.
// std::sort isn't stable, so the comparator carries a total-order tiebreak
// on the mac bytes: equal-RSSI peers come out in a deterministic order
// regardless of their input arrangement.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace lamp {

struct RosterEntry {
  std::string name;
  int8_t lastRssi = -127;
  uint8_t mac[6] = {0};
};

// Mirror of the getNear / getUngreetedArrivals comparator: RSSI descending,
// mac bytes ascending on a tie.
void sortByRssiThenMac(std::vector<RosterEntry>& out) {
  std::sort(out.begin(), out.end(),
            [](const RosterEntry& a, const RosterEntry& b) {
              if (a.lastRssi != b.lastRssi) return a.lastRssi > b.lastRssi;
              return std::memcmp(a.mac, b.mac, 6) < 0;
            });
}

RosterEntry make(const std::string& name, int8_t rssi, uint8_t lastMacByte) {
  RosterEntry e;
  e.name = name;
  e.lastRssi = rssi;
  e.mac[5] = lastMacByte;
  return e;
}

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

void test_sorts_rssi_descending() {
  std::vector<lamp::RosterEntry> v = {
      lamp::make("far", -90, 1),
      lamp::make("near", -40, 2),
      lamp::make("mid", -65, 3),
  };
  lamp::sortByRssiThenMac(v);
  TEST_ASSERT_EQUAL_STRING("near", v[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("mid", v[1].name.c_str());
  TEST_ASSERT_EQUAL_STRING("far", v[2].name.c_str());
}

void test_sentinel_sorts_to_back() {
  std::vector<lamp::RosterEntry> v = {
      lamp::make("unknown", -127, 1),
      lamp::make("weak", -95, 2),
      lamp::make("strong", -50, 3),
  };
  lamp::sortByRssiThenMac(v);
  TEST_ASSERT_EQUAL_STRING("strong", v[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("weak", v[1].name.c_str());
  TEST_ASSERT_EQUAL_STRING("unknown", v[2].name.c_str());
}

void test_equal_rssi_tiebreaks_on_mac_ascending() {
  std::vector<lamp::RosterEntry> v = {
      lamp::make("c", -60, 0x30),
      lamp::make("a", -60, 0x10),
      lamp::make("b", -60, 0x20),
  };
  lamp::sortByRssiThenMac(v);
  TEST_ASSERT_EQUAL_STRING("a", v[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("b", v[1].name.c_str());
  TEST_ASSERT_EQUAL_STRING("c", v[2].name.c_str());
}

void test_tiebreak_deterministic_across_input_permutations() {
  // std::sort isn't stable; the mac tiebreak must produce the same output
  // no matter how the equal-RSSI peers are arranged going in. Walk every
  // input permutation and assert the sorted result is always mac-ascending.
  std::vector<lamp::RosterEntry> v = {
      lamp::make("a", -60, 0x10),
      lamp::make("b", -60, 0x20),
      lamp::make("c", -60, 0x30),
  };
  std::sort(v.begin(), v.end(),
            [](const lamp::RosterEntry& x, const lamp::RosterEntry& y) {
              return x.name < y.name;
            });
  do {
    std::vector<lamp::RosterEntry> in = v;
    lamp::sortByRssiThenMac(in);
    std::string s;
    for (const auto& e : in) s += e.name;
    TEST_ASSERT_EQUAL_STRING("abc", s.c_str());
  } while (std::next_permutation(
      v.begin(), v.end(),
      [](const lamp::RosterEntry& x, const lamp::RosterEntry& y) {
        return x.name < y.name;
      }));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_sorts_rssi_descending);
  RUN_TEST(test_sentinel_sorts_to_back);
  RUN_TEST(test_equal_rssi_tiebreaks_on_mac_ascending);
  RUN_TEST(test_tiebreak_deterministic_across_input_permutations);
  return UNITY_END();
}
