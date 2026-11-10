// Native tests for LampInventory.
//
// Pins the contract:
//   - copyObservations fills (mac, rssi) in entry order and returns the count.
//   - copyObservations caps at `max`; null/zero args return 0.
//   - recordHello keeps the stored RSSI when the update carries INT8_MIN.
//   - prune drops aged entries from the observation feed.
//   - roster full → oldest entry evicted for the newcomer.

#include <unity.h>

#include <climits>
#include <cstdint>
#include <cstring>

#include "fleet/lamp_inventory.hpp"

namespace {

void mac(uint8_t out[6], uint8_t last) {
  const uint8_t m[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, last};
  std::memcpy(out, m, 6);
}

const uint8_t kColor[4] = {1, 2, 3, 4};

void hear(wisp::LampInventory& inv, uint8_t last, int8_t rssi, uint32_t nowMs) {
  uint8_t m[6];
  mac(m, last);
  inv.recordHello(m, "lamp", kColor, kColor, 1, nowMs, rssi);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_copy_observations_fills_mac_and_rssi(void) {
  wisp::LampInventory inv;
  hear(inv, 1, -60, 1000);
  hear(inv, 2, -75, 1000);

  wisp::LampObservation obs[4];
  const size_t n = inv.copyObservations(obs, 4);
  TEST_ASSERT_EQUAL_size_t(2, n);
  uint8_t m1[6], m2[6];
  mac(m1, 1);
  mac(m2, 2);
  TEST_ASSERT_EQUAL_MEMORY(m1, obs[0].mac, 6);
  TEST_ASSERT_EQUAL_INT8(-60, obs[0].rssi);
  TEST_ASSERT_EQUAL_MEMORY(m2, obs[1].mac, 6);
  TEST_ASSERT_EQUAL_INT8(-75, obs[1].rssi);
}

void test_copy_observations_caps_at_max(void) {
  wisp::LampInventory inv;
  for (uint8_t i = 1; i <= 5; i++) hear(inv, i, -60, 1000);

  wisp::LampObservation obs[3];
  TEST_ASSERT_EQUAL_size_t(3, inv.copyObservations(obs, 3));
  TEST_ASSERT_EQUAL_size_t(0, inv.copyObservations(obs, 0));
  TEST_ASSERT_EQUAL_size_t(0, inv.copyObservations(nullptr, 3));
}

void test_record_hello_preserves_rssi_on_unmeasured_update(void) {
  wisp::LampInventory inv;
  hear(inv, 1, -60, 1000);
  hear(inv, 1, INT8_MIN, 2000);

  wisp::LampObservation obs[1];
  TEST_ASSERT_EQUAL_size_t(1, inv.copyObservations(obs, 1));
  TEST_ASSERT_EQUAL_INT8(-60, obs[0].rssi);

  hear(inv, 1, -70, 3000);
  TEST_ASSERT_EQUAL_size_t(1, inv.copyObservations(obs, 1));
  TEST_ASSERT_EQUAL_INT8(-70, obs[0].rssi);
}

void test_prune_drops_aged_entries(void) {
  wisp::LampInventory inv;
  hear(inv, 1, -60, 1000);
  hear(inv, 2, -60, 50000);
  inv.prune(/*nowMs=*/61000, /*maxAgeMs=*/30000);

  wisp::LampObservation obs[2];
  const size_t n = inv.copyObservations(obs, 2);
  TEST_ASSERT_EQUAL_size_t(1, n);
  uint8_t m2[6];
  mac(m2, 2);
  TEST_ASSERT_EQUAL_MEMORY(m2, obs[0].mac, 6);
}

void test_full_roster_evicts_oldest(void) {
  wisp::LampInventory inv;
  for (size_t i = 0; i < wisp::LampInventory::MAX_LAMPS; i++) {
    hear(inv, static_cast<uint8_t>(i + 1), -60,
         1000 + static_cast<uint32_t>(i));
  }
  TEST_ASSERT_EQUAL_size_t(wisp::LampInventory::MAX_LAMPS, inv.size());

  hear(inv, 200, -60, 99000);
  TEST_ASSERT_EQUAL_size_t(wisp::LampInventory::MAX_LAMPS, inv.size());

  wisp::LampObservation obs[wisp::LampInventory::MAX_LAMPS];
  const size_t n = inv.copyObservations(obs, wisp::LampInventory::MAX_LAMPS);
  uint8_t oldest[6], newest[6];
  mac(oldest, 1);
  mac(newest, 200);
  bool sawOldest = false, sawNewest = false;
  for (size_t i = 0; i < n; i++) {
    if (std::memcmp(obs[i].mac, oldest, 6) == 0) sawOldest = true;
    if (std::memcmp(obs[i].mac, newest, 6) == 0) sawNewest = true;
  }
  TEST_ASSERT_FALSE(sawOldest);
  TEST_ASSERT_TRUE(sawNewest);
}

// Heard 10 s before the millis() wrap, pruned 5 s after it with a 30 s
// window: still fresh, so it stays. Past the window it goes.
void test_prune_ages_across_the_wrap(void) {
  wisp::LampInventory inv;
  hear(inv, 1, -60, UINT32_MAX - 9999);
  inv.prune(/*nowMs=*/5000, /*maxAgeMs=*/30000);
  TEST_ASSERT_EQUAL_size_t(1, inv.size());
  inv.prune(/*nowMs=*/20001, /*maxAgeMs=*/30000);
  TEST_ASSERT_EQUAL_size_t(0, inv.size());
}

// Last heard on the very millisecond millis() wraps to 0, then silent: prune
// drops it once the window passes, like any other stamp.
void test_prune_drops_a_lamp_last_heard_at_zero(void) {
  wisp::LampInventory inv;
  hear(inv, 1, -60, 0);
  inv.prune(/*nowMs=*/30000, /*maxAgeMs=*/30000);
  TEST_ASSERT_EQUAL_size_t(1, inv.size());
  inv.prune(/*nowMs=*/30001, /*maxAgeMs=*/30000);
  TEST_ASSERT_EQUAL_size_t(0, inv.size());
}

// A full inventory heard before the wrap; lamp 1, the first heard, is heard
// again after it. The next newcomer evicts lamp 2, now the stalest.
void test_full_roster_evicts_stalest_across_the_wrap(void) {
  wisp::LampInventory inv;
  const uint32_t start = UINT32_MAX - 100000;
  for (size_t i = 0; i < wisp::LampInventory::MAX_LAMPS; i++) {
    hear(inv, static_cast<uint8_t>(i + 1), -60, start + static_cast<uint32_t>(i) * 10);
  }
  hear(inv, 1, -60, 100);
  hear(inv, 200, -60, 200);

  wisp::LampObservation obs[wisp::LampInventory::MAX_LAMPS];
  const size_t n = inv.copyObservations(obs, wisp::LampInventory::MAX_LAMPS);
  uint8_t first[6], second[6];
  mac(first, 1);
  mac(second, 2);
  bool sawFirst = false, sawSecond = false;
  for (size_t i = 0; i < n; i++) {
    if (std::memcmp(obs[i].mac, first, 6) == 0) sawFirst = true;
    if (std::memcmp(obs[i].mac, second, 6) == 0) sawSecond = true;
  }
  TEST_ASSERT_TRUE(sawFirst);
  TEST_ASSERT_FALSE(sawSecond);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_copy_observations_fills_mac_and_rssi);
  RUN_TEST(test_copy_observations_caps_at_max);
  RUN_TEST(test_record_hello_preserves_rssi_on_unmeasured_update);
  RUN_TEST(test_prune_drops_aged_entries);
  RUN_TEST(test_full_roster_evicts_oldest);
  RUN_TEST(test_prune_ages_across_the_wrap);
  RUN_TEST(test_prune_drops_a_lamp_last_heard_at_zero);
  RUN_TEST(test_full_roster_evicts_stalest_across_the_wrap);
  return UNITY_END();
}
