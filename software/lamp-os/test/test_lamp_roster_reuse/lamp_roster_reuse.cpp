// Native-host proof that the roster snapshot buffers never reallocate after
// their one reservation: snapshotNear/getMesh are reserved to kCapacity at
// construction. A growing roster or a steady-state query loop must keep a
// stable data() pointer and capacity, which is what keeps the fragmented lamp
// heap's largest free block high. getAll is sized to the roster's count and
// reallocates only when the count moves.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Native-test seam: exercise the shipped class.
#include "components/network/mesh/lamp_roster.cpp"

using namespace lamp;

namespace {

const Color kNoColor = Color();

void seed(LampRoster& r, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, i};
    r.addOrUpdateFromEspNow("peer", mac, kNoColor, kNoColor, 0x010203, 0, 5,
                            nullptr, nullptr, false, 0, false, nullptr, false,
                            -60);
    r.addOrUpdateFromBle("peer", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -50);
  }
}

// Re-touch the same MACs so count_ stays fixed; a growing roster would
// legitimately grow the buffer, which would mask a per-call realloc.
void churn(LampRoster& r, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, i};
    r.addOrUpdateFromEspNow("peer", mac, kNoColor, kNoColor, 0x010203, 0, 5,
                            nullptr, nullptr, false, 0, false, nullptr, false,
                            -60);
  }
}

// Add `n` distinct near peers via the BLE path (each sighting sets the near
// timestamp; distinct addresses recover distinct MACs, so count_ == n).
void addNear(LampRoster& r, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    char addr[18];
    std::snprintf(addr, sizeof(addr), "AA:BB:CC:DD:EE:%02X", i);
    r.addOrUpdateFromBle("peer", addr, kNoColor, kNoColor, -50);
  }
}

}  // namespace

void setUp(void) { set_mock_millis(100000); }
void tearDown(void) {}

void test_getters_return_stable_buffer_reference() {
  LampRoster r;
  seed(r, 8);
  TEST_ASSERT_EQUAL_PTR(&r.getMesh(240000), &r.getMesh(240000));
  TEST_ASSERT_EQUAL_PTR(&r.getAll(), &r.getAll());
}

void test_steady_state_queries_do_not_realloc() {
  LampRoster r;
  const uint8_t n = 12;
  seed(r, n);

  const MeshPeer* meshData = r.getMesh(240000).data();
  size_t meshCap = r.getMesh(240000).capacity();
  const ListedPeer* allData = r.getAll().data();
  size_t allCap = r.getAll().capacity();

  // A non-empty result confirms the buffers actually carry entries, so a
  // stable pointer means reuse, not an always-empty vector.
  TEST_ASSERT_TRUE(r.getMesh(240000).size() > 0);

  for (int i = 0; i < 2000; i++) {
    churn(r, n);
    auto& mesh = r.getMesh(240000);
    auto& all = r.getAll();
    TEST_ASSERT_EQUAL_PTR(meshData, mesh.data());
    TEST_ASSERT_EQUAL_size_t(meshCap, mesh.capacity());
    TEST_ASSERT_EQUAL_PTR(allData, all.data());
    TEST_ASSERT_EQUAL_size_t(allCap, all.capacity());
  }
}

// Growing the roster 0 -> kCapacity must not move the near or mesh buffer:
// each is reserved to kCapacity up front, so a new roster high-water mark never
// reallocates (the per-growth staircase is what fragments the lamp heap).
void test_buffers_do_not_realloc_as_roster_grows_to_capacity() {
  LampRoster r;
  const NearbyCopy* nearData = r.snapshotNear(240000).data();
  const MeshPeer* meshData = r.getMesh(240000).data();
  TEST_ASSERT_EQUAL_size_t(LampRoster::kCapacity, r.snapshotNear(240000).capacity());
  TEST_ASSERT_EQUAL_size_t(LampRoster::kCapacity, r.getMesh(240000).capacity());

  uint32_t now = 100000;
  for (size_t i = 0; i < LampRoster::kCapacity; i++) {
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x01, static_cast<uint8_t>(i)};
    r.addOrUpdateFromEspNow("peer", mac, kNoColor, kNoColor, 0x010203, 0, 5,
                            nullptr, nullptr, false, 0, false, nullptr, false,
                            -60);
    r.markNear(mac);
    now += LampRoster::kSnapshotCacheMs;  // defeat the snapshotNear cache
    set_mock_millis(now);
    TEST_ASSERT_EQUAL_size_t(i + 1, r.snapshotNear(240000).size());
    TEST_ASSERT_EQUAL_size_t(i + 1, r.getMesh(240000).size());
    TEST_ASSERT_EQUAL_size_t(i + 1, r.getAll().size());
    TEST_ASSERT_EQUAL_PTR(nearData, r.snapshotNear(240000).data());
    TEST_ASSERT_EQUAL_PTR(meshData, r.getMesh(240000).data());
  }
}

void addMesh(LampRoster& r, uint8_t from, uint8_t to) {
  for (uint8_t i = from; i < to; i++) {
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x02, i};
    r.addOrUpdateFromEspNow("peer", mac, kNoColor, kNoColor, 0x010203, 0, 5,
                            nullptr, nullptr, false, 0, false, nullptr, false,
                            -60);
  }
}

void test_get_all_is_sized_to_the_roster() {
  LampRoster r;
  TEST_ASSERT_EQUAL_size_t(0, r.getAll().capacity());
  set_mock_millis(100000);
  addMesh(r, 0, 22);
  TEST_ASSERT_EQUAL_size_t(22, r.getAll().size());
  TEST_ASSERT_EQUAL_size_t(22, r.getAll().capacity());
  addMesh(r, 22, 30);
  TEST_ASSERT_EQUAL_size_t(30, r.getAll().size());
  TEST_ASSERT_EQUAL_size_t(30, r.getAll().capacity());
  set_mock_millis(400000);
  addMesh(r, 20, 30);
  r.prune(240000);
  TEST_ASSERT_EQUAL_size_t(10, r.getAll().size());
  TEST_ASSERT_EQUAL_size_t(10, r.getAll().capacity());
}

void test_get_all_resizes_after_release() {
  LampRoster r;
  addMesh(r, 0, 4);
  TEST_ASSERT_EQUAL_size_t(4, r.getAll().capacity());
  r.releaseAll();
  auto& all = r.getAll();
  TEST_ASSERT_EQUAL_size_t(4, all.capacity());
  TEST_ASSERT_EQUAL_size_t(r.size(), all.size());
}

void test_snapshot_near_reuses_cached_copy_within_window() {
  LampRoster r;
  set_mock_millis(100000);
  addNear(r, 1);
  TEST_ASSERT_EQUAL_size_t(1, r.snapshotNear(240000).size());

  // A mutation inside the freshness window is not reflected: the cached copy
  // comes back unchanged.
  addNear(r, 2);
  TEST_ASSERT_EQUAL_size_t(1, r.snapshotNear(240000).size());

  // Past the window, the rebuild reflects the mutation.
  set_mock_millis(100000 + LampRoster::kSnapshotCacheMs);
  TEST_ASSERT_EQUAL_size_t(2, r.snapshotNear(240000).size());
}

void test_snapshot_near_rebuilds_on_different_max_age() {
  LampRoster r;
  set_mock_millis(100000);
  addNear(r, 1);
  TEST_ASSERT_EQUAL_size_t(1, r.snapshotNear(240000).size());

  // Same instant, different maxAgeMs: the age-filter differs, so the cache
  // must not be reused even though the window has not elapsed.
  addNear(r, 2);
  TEST_ASSERT_EQUAL_size_t(2, r.snapshotNear(120000).size());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_getters_return_stable_buffer_reference);
  RUN_TEST(test_steady_state_queries_do_not_realloc);
  RUN_TEST(test_buffers_do_not_realloc_as_roster_grows_to_capacity);
  RUN_TEST(test_get_all_is_sized_to_the_roster);
  RUN_TEST(test_get_all_resizes_after_release);
  RUN_TEST(test_snapshot_near_reuses_cached_copy_within_window);
  RUN_TEST(test_snapshot_near_rebuilds_on_different_max_age);
  return UNITY_END();
}
