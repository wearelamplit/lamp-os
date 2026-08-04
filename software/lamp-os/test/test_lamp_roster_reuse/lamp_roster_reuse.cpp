// Native-host proof that the roster snapshot getters reuse their internal
// buffers instead of allocating per call. getNear/getMesh/getAll return a
// reference to a member vector refilled in place; a steady-state query loop
// must not realloc (stable capacity + stable data() pointer), which is what
// keeps the fragmented lamp heap's largest free block high.

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
  TEST_ASSERT_EQUAL_PTR(&r.getNear(240000), &r.getNear(240000));
  TEST_ASSERT_EQUAL_PTR(&r.getMesh(240000), &r.getMesh(240000));
  TEST_ASSERT_EQUAL_PTR(&r.getAll(), &r.getAll());
}

void test_steady_state_queries_do_not_realloc() {
  LampRoster r;
  const uint8_t n = 12;
  seed(r, n);

  // Warm up: first call grows each buffer to the roster size.
  const RosterEntry* nearData = r.getNear(240000).data();
  size_t nearCap = r.getNear(240000).capacity();
  const RosterEntry* meshData = r.getMesh(240000).data();
  size_t meshCap = r.getMesh(240000).capacity();
  const RosterEntry* allData = r.getAll().data();
  size_t allCap = r.getAll().capacity();

  // A non-empty result confirms the buffers actually carry entries, so a
  // stable pointer means reuse, not an always-empty vector.
  TEST_ASSERT_TRUE(r.getMesh(240000).size() > 0);

  for (int i = 0; i < 2000; i++) {
    churn(r, n);
    auto& near = r.getNear(240000);
    auto& mesh = r.getMesh(240000);
    auto& all = r.getAll();
    TEST_ASSERT_EQUAL_PTR(nearData, near.data());
    TEST_ASSERT_EQUAL_size_t(nearCap, near.capacity());
    TEST_ASSERT_EQUAL_PTR(meshData, mesh.data());
    TEST_ASSERT_EQUAL_size_t(meshCap, mesh.capacity());
    TEST_ASSERT_EQUAL_PTR(allData, all.data());
    TEST_ASSERT_EQUAL_size_t(allCap, all.capacity());
  }
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
  RUN_TEST(test_snapshot_near_reuses_cached_copy_within_window);
  RUN_TEST(test_snapshot_near_rebuilds_on_different_max_age);
  return UNITY_END();
}
