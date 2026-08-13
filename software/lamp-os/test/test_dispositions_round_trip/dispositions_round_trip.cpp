// Native-host unit test confirming the existing Dispositions storage
// is correct when keyed by BD_ADDR strings instead of names. This is
// a regression check on the surface change: the in-memory data
// structure (sorted vector of pair<string, uint8_t>) doesn't change;
// the keys just look different. We verify set/get round-trips on
// real BD_ADDR-shaped strings preserve disposition values across the
// (typically alphabetic) sort, including the lower_bound-equality
// edge case from the original test_dispositions suite.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lamp {

// Mirror of Config's sorted-vector dispositions store. Identical shape
// to the one in test_dispositions/dispositions.cpp (we'd factor this
// into a shared header, but PlatformIO's native test discovery treats
// each test_* dir as independent — duplicating is the cheap path).
class Dispositions {
 public:
  static constexpr uint8_t kDefault = 3;
  static constexpr size_t kMax = 100;

  uint8_t get(const std::string& key) const {
    auto it = std::lower_bound(
        entries_.begin(), entries_.end(), key,
        [](const std::pair<std::string, uint8_t>& a, const std::string& b) {
          return a.first < b;
        });
    if (it == entries_.end() || it->first != key) return kDefault;
    return it->second;
  }

  void set(const std::string& key, uint8_t value) {
    if (key.empty()) return;
    if (value < 1) value = 1;
    if (value > 5) value = 5;
    auto it = std::lower_bound(
        entries_.begin(), entries_.end(), key,
        [](const std::pair<std::string, uint8_t>& a, const std::string& b) {
          return a.first < b;
        });
    if (it != entries_.end() && it->first == key) {
      it->second = value;
      return;
    }
    if (entries_.size() >= kMax) {
      entries_.erase(entries_.begin());
      it = std::lower_bound(
          entries_.begin(), entries_.end(), key,
          [](const std::pair<std::string, uint8_t>& a, const std::string& b) {
            return a.first < b;
          });
    }
    entries_.insert(it, std::make_pair(key, value));
  }

  size_t size() const { return entries_.size(); }
  const std::vector<std::pair<std::string, uint8_t>>& entries() const {
    return entries_;
  }

 private:
  std::vector<std::pair<std::string, uint8_t>> entries_;
};

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

void test_bd_addr_round_trip_preserves_values() {
  // Set + get with real BD_ADDR strings. Confirms the surface change
  // (string keys now look like BD_ADDR) doesn't break lookup.
  lamp::Dispositions d;
  d.set("AA:BB:CC:DD:EE:FF", 4);  // fond
  d.set("11:22:33:44:55:66", 1);  // salty
  d.set("FF:EE:DD:CC:BB:AA", 5);  // smitten

  TEST_ASSERT_EQUAL_UINT8(4, d.get("AA:BB:CC:DD:EE:FF"));
  TEST_ASSERT_EQUAL_UINT8(1, d.get("11:22:33:44:55:66"));
  TEST_ASSERT_EQUAL_UINT8(5, d.get("FF:EE:DD:CC:BB:AA"));
  // Unset BD_ADDR returns default.
  TEST_ASSERT_EQUAL_UINT8(3, d.get("00:00:00:00:00:00"));
}

void test_bd_addr_sort_order_correct() {
  // The internal sort is lexicographic on the BD_ADDR string. Confirm
  // lookup works correctly across the sort boundary — specifically the
  // lower_bound landing-on-neighbour edge case from the original suite.
  lamp::Dispositions d;
  d.set("AA:BB:CC:DD:EE:FF", 5);
  d.set("CC:DD:EE:FF:00:11", 2);

  // Sorted order: AA... < CC...
  const auto& v = d.entries();
  TEST_ASSERT_EQUAL_UINT32(2, v.size());
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", v[0].first.c_str());
  TEST_ASSERT_EQUAL_STRING("CC:DD:EE:FF:00:11", v[1].first.c_str());

  // Looking up a BD_ADDR that sorts BETWEEN the two existing keys must
  // NOT return the second key's value — the production code's
  // it->first == key check guards this. Pre-Phase-C test_dispositions
  // had the same case for name strings; we re-cover for BD_ADDR shape.
  TEST_ASSERT_EQUAL_UINT8(3, d.get("BB:00:00:00:00:00"));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_bd_addr_round_trip_preserves_values);
  RUN_TEST(test_bd_addr_sort_order_correct);

  return UNITY_END();
}
