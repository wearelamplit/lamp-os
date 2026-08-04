// Native-host proof of the roster's re-entrancy safety, exercised with
// LAMP_DEBUG on:
//   1. snapshotNear() isolates the caller from a re-entrant getNear(): a
//      callback that refills the shared buffer mid-iteration cannot corrupt
//      or mutate the stack snapshot the caller is walking.
//   2. the debug getter guard fires on an unexpected-task call and on a
//      re-entrant fill, so a future off-loop-task or nested caller fails fast
//      on the bench instead of silently corrupting the field heap.

#define LAMP_DEBUG 1

#include <unity.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "components/network/mesh/lamp_roster.cpp"

using namespace lamp;

namespace {

const Color kNoColor = Color();

const char* g_lastReason = nullptr;
int g_violations = 0;
void recordViolation(const char* reason) {
  g_lastReason = reason;
  ++g_violations;
}

// Seed `n` distinct NEAR peers (BLE path sets lastSeenNearMs + a recovered
// mac), descending RSSI so getNear's sort order is deterministic.
void seedNear(LampRoster& r, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    char addr[18];
    std::snprintf(addr, sizeof(addr), "02:00:00:00:00:%02X", i);
    r.addOrUpdateFromBle("peer", addr, kNoColor, kNoColor,
                         static_cast<int8_t>(-40 - i));
  }
}

// Seed `n` distinct MESH peers (ESP-NOW path sets lastSeenMeshMs) so getMesh
// returns a non-empty refill.
void seedMesh(LampRoster& r, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    uint8_t mac[6] = {0x03, 0x00, 0x00, 0x00, 0x00, i};
    r.addOrUpdateFromEspNow("peer", mac, kNoColor, kNoColor, 0x010203, 0, 5,
                            nullptr, nullptr, false, 0, false, nullptr, false,
                            -60);
  }
}

}  // namespace

void setUp(void) {
  set_mock_millis(100000);
  set_mock_current_task(reinterpret_cast<void*>(1));
  LampRoster::s_debugGetterViolation = recordViolation;
  g_lastReason = nullptr;
  g_violations = 0;
}
void tearDown(void) { LampRoster::s_debugGetterViolation = nullptr; }

void test_snapshot_survives_reentrant_getNear() {
  LampRoster r;
  seedNear(r, 5);

  std::vector<NearbyCopy> snap = r.snapshotNear(240000);
  size_t n = snap.size();
  TEST_ASSERT_TRUE(n > 0);

  // The snapshot must match getNear's order/content (correct results).
  {
    auto& near = r.getNear(240000);
    TEST_ASSERT_EQUAL_size_t(near.size(), n);
    for (size_t i = 0; i < n; i++) {
      TEST_ASSERT_EQUAL_MEMORY(near[i].mac, snap[i].mac, 6);
      TEST_ASSERT_EQUAL_INT8(near[i].lastRssi, snap[i].lastRssi);
    }
  }

  std::vector<std::array<uint8_t, 6>> expected;
  for (size_t i = 0; i < n; i++) {
    std::array<uint8_t, 6> m{};
    std::memcpy(m.data(), snap[i].mac, 6);
    expected.push_back(m);
  }

  // Simulate a callback re-entering getNear on each iteration: it clears and
  // refills the shared nearBuf_. The stack snapshot must be untouched.
  for (size_t i = 0; i < n; i++) {
    auto& reentrant = r.getNear(240000);
    (void)reentrant;
    TEST_ASSERT_EQUAL_MEMORY(expected[i].data(), snap[i].mac, 6);
  }
  TEST_ASSERT_EQUAL_INT(0, g_violations);  // same task, no nesting: clean
}

void test_guard_fires_on_unexpected_task() {
  LampRoster r;
  seedNear(r, 3);

  set_mock_current_task(reinterpret_cast<void*>(1));
  r.getNear(240000);  // captures task 1
  TEST_ASSERT_EQUAL_INT(0, g_violations);

  set_mock_current_task(reinterpret_cast<void*>(2));
  r.getNear(240000);  // wrong task
  TEST_ASSERT_EQUAL_INT(1, g_violations);
  TEST_ASSERT_NOT_NULL(g_lastReason);
}

// The RETURNED REFERENCE stays valid while a callback re-enters getNear/getMesh
// mid-walk: those refill distinct buffers, so the in-flight snapshot keeps its
// size and content even as the live roster grows underneath. Fails if
// nearSnapshot_ ever aliases nearBuf_/meshBuf_.
void test_snapshot_reference_stable_across_reentrant_getters() {
  LampRoster r;
  seedNear(r, 5);
  seedMesh(r, 5);

  const std::vector<NearbyCopy>& near = r.snapshotNear(240000);
  const size_t n = near.size();
  TEST_ASSERT_EQUAL_size_t(5, n);

  std::vector<std::array<uint8_t, 6>> expected;
  for (const NearbyCopy& e : near) {
    std::array<uint8_t, 6> m{};
    std::memcpy(m.data(), e.mac, 6);
    expected.push_back(m);
  }

  bool grew = false;
  for (size_t i = 0; i < near.size(); i++) {
    if (!grew) {
      seedNear(r, 25);  // roster now far larger than the held snapshot
      grew = true;
    }
    auto& reMesh = r.getMesh(240000);
    auto& reNear = r.getNear(240000);
    (void)reMesh;
    TEST_ASSERT_TRUE(reNear.size() > n);  // the re-entrant getter sees the growth
    TEST_ASSERT_EQUAL_size_t(n, near.size());  // the held reference does not
    TEST_ASSERT_EQUAL_MEMORY(expected[i].data(), near[i].mac, 6);
  }
  TEST_ASSERT_EQUAL_INT(0, g_violations);
}

// The near-walk honors the real roster ceiling, not a 16-entry snapshot cap:
// seed more near peers than the old cap and assert every one is visited.
void test_snapshot_visits_all_near_above_old_cap() {
  LampRoster r;
  const uint8_t n = 30;
  seedNear(r, n);

  const std::vector<NearbyCopy>& snap = r.snapshotNear(240000);
  TEST_ASSERT_EQUAL_size_t(n, snap.size());
}

void test_guard_fires_on_reentrant_fill() {
  LampRoster r;
  set_mock_current_task(reinterpret_cast<void*>(1));

  r.debugGetterEnter();  // captures task, marks in-use
  TEST_ASSERT_EQUAL_INT(0, g_violations);
  r.debugGetterEnter();  // re-entered before exit
  TEST_ASSERT_EQUAL_INT(1, g_violations);
  TEST_ASSERT_NOT_NULL(g_lastReason);
  r.debugGetterExit();
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_snapshot_survives_reentrant_getNear);
  RUN_TEST(test_snapshot_reference_stable_across_reentrant_getters);
  RUN_TEST(test_snapshot_visits_all_near_above_old_cap);
  RUN_TEST(test_guard_fires_on_unexpected_task);
  RUN_TEST(test_guard_fires_on_reentrant_fill);
  return UNITY_END();
}
