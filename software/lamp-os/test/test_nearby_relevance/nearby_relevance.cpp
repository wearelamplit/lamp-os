// Pins the nearby-list relevance order (ble_control's top-N cap): near before
// mesh-only, then most-recently-seen, then strongest RSSI. A broken comparator
// is UB in std::partial_sort, so this checks the strict-weak-ordering axioms
// and the "a near peer never loses its slot to a mesh-only peer" invariant.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "components/network/mesh/proximity.hpp"

namespace {

constexpr uint32_t kNow      = 1'000'000;
constexpr uint32_t kMaxAgeMs = 240000;

struct Entry {
  uint32_t lastSeenNearMs = 0;
  uint32_t lastSeenMeshMs = 0;
  int8_t   lastRssi       = -127;
};

bool cmp(const Entry& a, const Entry& b) {
  return lamp::nearbyMoreRelevant(a, b, kNow, kMaxAgeMs);
}

Entry nearE(uint32_t seen, int8_t rssi) { return {seen, 0, rssi}; }
Entry meshE(uint32_t seen) { return {0, seen, -127}; }

void setUp() {}
void tearDown() {}

// Irreflexive: an entry is never more-relevant than itself.
void test_irreflexive() {
  Entry a = nearE(kNow - 100, -50);
  TEST_ASSERT_FALSE(cmp(a, a));
}

// Asymmetric: at most one direction holds for a distinct pair.
void test_asymmetric() {
  Entry a = nearE(kNow - 100, -40);
  Entry b = meshE(kNow - 50);
  TEST_ASSERT_FALSE(cmp(a, b) && cmp(b, a));
  // Equivalent pair (all fields equal): neither direction holds.
  Entry c = nearE(kNow - 100, -40);
  Entry d = nearE(kNow - 100, -40);
  TEST_ASSERT_FALSE(cmp(c, d));
  TEST_ASSERT_FALSE(cmp(d, c));
}

// Transitive: a<b, b<c => a<c across the three tie levels.
void test_transitive() {
  Entry a = nearE(kNow - 10, -30);   // near, most recent, strongest
  Entry b = nearE(kNow - 20, -30);   // near, older
  Entry c = meshE(kNow - 5);         // mesh-only, loses on the near flag
  TEST_ASSERT_TRUE(cmp(a, b));
  TEST_ASSERT_TRUE(cmp(b, c));
  TEST_ASSERT_TRUE(cmp(a, c));
}

// A near peer outranks a mesh-only peer even when the mesh peer is far more
// recent and the near peer has a weak RSSI: the cap must not drop near peers.
void test_near_beats_mesh_regardless() {
  Entry nearWeakOld = nearE(kNow - 200000, -90);
  Entry meshFresh   = meshE(kNow - 1);
  TEST_ASSERT_TRUE(cmp(nearWeakOld, meshFresh));
  TEST_ASSERT_FALSE(cmp(meshFresh, nearWeakOld));
}

// Sorting a mixed set lands every near entry ahead of every mesh-only entry.
void test_sort_partitions_near_first() {
  std::vector<Entry> v = {
      meshE(kNow - 5), nearE(kNow - 300, -70), meshE(kNow - 2),
      nearE(kNow - 10, -50), meshE(kNow - 1), nearE(kNow - 100, -60),
  };
  std::sort(v.begin(), v.end(), cmp);
  bool sawMesh = false;
  for (const auto& e : v) {
    const bool isNear = lamp::isNearNow(e.lastSeenNearMs, kNow, kMaxAgeMs);
    if (!isNear) sawMesh = true;
    else TEST_ASSERT_FALSE_MESSAGE(sawMesh, "near entry sorted after a mesh-only one");
  }
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_irreflexive);
  RUN_TEST(test_asymmetric);
  RUN_TEST(test_transitive);
  RUN_TEST(test_near_beats_mesh_regardless);
  RUN_TEST(test_sort_partitions_near_first);
  return UNITY_END();
}
