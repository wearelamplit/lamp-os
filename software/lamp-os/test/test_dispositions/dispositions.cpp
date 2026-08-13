// Native-host unit tests for the sorted-vector dispositions store.
//
// Context: audit finding [MEDIUM] flagged `Config`'s
// `std::map<std::string, uint8_t> dispositions_` for up to 100 entries
// (~3.2 KB + string overhead + red-black-tree pointer chasing per lookup).
// Replaced with a `std::vector<std::pair<std::string, uint8_t>>` kept
// sorted by name; lookups use `std::lower_bound`. Saves ~1.5 KB at full
// capacity and gives cache-friendly contiguous storage.
//
// Following test_section_cache / test_disposition_debounce convention:
// the storage helper is reimplemented inline here so the native test
// doesn't try to link against `Config` (which transitively pulls in
// Arduino / Preferences / NimBLE). The shape mirrors the (sorted-vector,
// lower_bound, cap-enforcing insert) policy in src/config/config.{hpp,cpp};
// if you change the policy there, mirror here.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lamp {

// Mirror of Config's dispositions storage. Same invariants:
//   - Entries are always kept sorted by name (lexicographic).
//   - get(name) returns kDefault if not present, else the stored value.
//   - set(name, value) clamps to [1,5], updates in place if present,
//     inserts in sorted position otherwise. At capacity, an insert of a
//     NEW name evicts entries[0] (lowest-by-name) — matching the
//     production policy ("first by std::map iteration order" was
//     alphabetical-by-name, so the vector behaves identically).
//   - clear() empties the store.
class Dispositions {
 public:
  static constexpr uint8_t kDefault = 3;
  static constexpr size_t kMax = 100;

  uint8_t get(const std::string& name) const {
    auto it = std::lower_bound(
        entries_.begin(), entries_.end(), name,
        [](const std::pair<std::string, uint8_t>& a, const std::string& b) {
          return a.first < b;
        });
    if (it == entries_.end() || it->first != name) return kDefault;
    return it->second;
  }

  void set(const std::string& name, uint8_t value) {
    if (name.empty()) return;
    if (value < 1) value = 1;
    if (value > 5) value = 5;
    auto it = std::lower_bound(
        entries_.begin(), entries_.end(), name,
        [](const std::pair<std::string, uint8_t>& a, const std::string& b) {
          return a.first < b;
        });
    if (it != entries_.end() && it->first == name) {
      it->second = value;
      return;
    }
    if (entries_.size() >= kMax) {
      // Evict lowest-by-name to mirror std::map iteration-order eviction.
      entries_.erase(entries_.begin());
      // Recompute insertion point — erase may have shifted it.
      it = std::lower_bound(
          entries_.begin(), entries_.end(), name,
          [](const std::pair<std::string, uint8_t>& a, const std::string& b) {
            return a.first < b;
          });
    }
    entries_.insert(it, std::make_pair(name, value));
  }

  void clear() { entries_.clear(); }
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

void test_get_missing_returns_default() {
  // Fresh store, no entries — every lookup is kDefault (3 = neutral).
  // This matches the legacy std::map behaviour and is the contract
  // SocialBehavior relies on for un-rated peers.
  lamp::Dispositions d;
  TEST_ASSERT_EQUAL_UINT8(3, d.get("anyone"));
  TEST_ASSERT_EQUAL_UINT8(3, d.get(""));
}

void test_set_then_get_roundtrips() {
  lamp::Dispositions d;
  d.set("alice", 5);
  d.set("bob", 1);
  d.set("carol", 4);
  TEST_ASSERT_EQUAL_UINT8(5, d.get("alice"));
  TEST_ASSERT_EQUAL_UINT8(1, d.get("bob"));
  TEST_ASSERT_EQUAL_UINT8(4, d.get("carol"));
}

void test_set_clamps_to_valid_range() {
  // [1,5] is the wire contract — out-of-range values clamp at the
  // boundaries rather than rejecting. Matches the JSON-load and BLE-write
  // paths in config.cpp which both clamp.
  lamp::Dispositions d;
  d.set("low", 0);
  d.set("high", 99);
  TEST_ASSERT_EQUAL_UINT8(1, d.get("low"));
  TEST_ASSERT_EQUAL_UINT8(5, d.get("high"));
}

void test_set_ignores_empty_name() {
  // Empty peerName is never a valid key — the production setDisposition
  // rejects it early to avoid a poison "" entry from corrupted JSON.
  lamp::Dispositions d;
  d.set("", 4);
  TEST_ASSERT_EQUAL_UINT32(0, d.size());
  TEST_ASSERT_EQUAL_UINT8(3, d.get(""));
}

void test_insert_maintains_sort_order() {
  // The whole point of the refactor: lookups use std::lower_bound, which
  // requires the underlying vector to be sorted by key. Insert in
  // out-of-order sequence, then verify entries() comes out sorted.
  lamp::Dispositions d;
  d.set("charlie", 3);
  d.set("alice", 5);
  d.set("bob", 2);
  d.set("dave", 1);

  const auto& v = d.entries();
  TEST_ASSERT_EQUAL_UINT32(4, v.size());
  TEST_ASSERT_EQUAL_STRING("alice", v[0].first.c_str());
  TEST_ASSERT_EQUAL_STRING("bob", v[1].first.c_str());
  TEST_ASSERT_EQUAL_STRING("charlie", v[2].first.c_str());
  TEST_ASSERT_EQUAL_STRING("dave", v[3].first.c_str());
}

void test_update_in_place_preserves_order_and_size() {
  // Re-setting an existing name updates the value but doesn't add a new
  // entry — guards against accidental O(N) insertion of duplicates if
  // the lower_bound check is wrong.
  lamp::Dispositions d;
  d.set("alice", 5);
  d.set("bob", 3);
  d.set("alice", 1);  // update

  TEST_ASSERT_EQUAL_UINT32(2, d.size());
  TEST_ASSERT_EQUAL_UINT8(1, d.get("alice"));
  TEST_ASSERT_EQUAL_UINT8(3, d.get("bob"));
}

void test_get_miss_between_entries() {
  // lower_bound returns an iterator that may point at a different name.
  // Specifically: looking up "bob" in {alice, charlie} returns the
  // iterator at "charlie" — the production code must compare it->first
  // == name before returning, else it returns charlie's value.
  lamp::Dispositions d;
  d.set("alice", 5);
  d.set("charlie", 2);
  TEST_ASSERT_EQUAL_UINT8(3, d.get("bob"));  // default, NOT 2
}

void test_capacity_enforced_at_max() {
  // 100-entry cap from kDispositionsMax. A 101st distinct name must
  // evict the lowest-by-name entry to make room. We don't grow past max.
  lamp::Dispositions d;
  // Fill to kMax with names that sort lexicographically.
  for (int i = 0; i < (int)lamp::Dispositions::kMax; ++i) {
    char name[8];
    // names like "p000".."p099" so lexicographic order matches numeric.
    snprintf(name, sizeof(name), "p%03d", i);
    d.set(name, 4);
  }
  TEST_ASSERT_EQUAL_UINT32(lamp::Dispositions::kMax, d.size());
  TEST_ASSERT_EQUAL_UINT8(4, d.get("p000"));
  TEST_ASSERT_EQUAL_UINT8(4, d.get("p099"));

  // Insert one MORE (a new name that sorts AFTER everything). p000
  // (lowest) is evicted to make room.
  d.set("zzz_new", 2);
  TEST_ASSERT_EQUAL_UINT32(lamp::Dispositions::kMax, d.size());
  TEST_ASSERT_EQUAL_UINT8(3, d.get("p000"));  // evicted → returns default
  TEST_ASSERT_EQUAL_UINT8(4, d.get("p001"));  // still present
  TEST_ASSERT_EQUAL_UINT8(2, d.get("zzz_new"));
}

void test_capacity_update_in_place_does_not_evict() {
  // Updating an EXISTING name at capacity must NOT evict — the cap only
  // gates net-new inserts. Otherwise dragging a slider on a full lamp
  // would silently delete entries.
  lamp::Dispositions d;
  for (int i = 0; i < (int)lamp::Dispositions::kMax; ++i) {
    char name[8];
    snprintf(name, sizeof(name), "p%03d", i);
    d.set(name, 4);
  }
  // Update an existing name. No eviction expected.
  d.set("p050", 1);

  TEST_ASSERT_EQUAL_UINT32(lamp::Dispositions::kMax, d.size());
  TEST_ASSERT_EQUAL_UINT8(4, d.get("p000"));  // still present, NOT evicted
  TEST_ASSERT_EQUAL_UINT8(1, d.get("p050"));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_get_missing_returns_default);
  RUN_TEST(test_set_then_get_roundtrips);
  RUN_TEST(test_set_clamps_to_valid_range);
  RUN_TEST(test_set_ignores_empty_name);
  RUN_TEST(test_insert_maintains_sort_order);
  RUN_TEST(test_update_in_place_preserves_order_and_size);
  RUN_TEST(test_get_miss_between_entries);
  RUN_TEST(test_capacity_enforced_at_max);
  RUN_TEST(test_capacity_update_in_place_does_not_evict);

  return UNITY_END();
}
