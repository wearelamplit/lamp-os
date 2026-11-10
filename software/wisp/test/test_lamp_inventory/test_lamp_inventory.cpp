// Native tests for LampInventory.
//
// Pins the contract:
//   - copyObservations fills (mac, rssi) in entry order and returns the count.
//   - copyObservations caps at `max`; null/zero args return 0.
//   - recordHello keeps the stored RSSI when the update carries INT8_MIN.
//   - prune forgets a direct RSSI older than kDirectRssiMaxAgeMs, relays or not,
//     and keeps one heard within it, across the millis() wrap and when the
//     stamp sits just ahead of nowMs.
//   - kDirectRssiMaxAgeMs derives from the lamp's HELLO interval.
//   - prune drops aged entries from the observation feed.
//   - roster full → oldest entry evicted for the newcomer.

#include <unity.h>

#include <climits>
#include <cstdint>
#include <cstring>

#include "fleet/lamp_inventory.hpp"
#include "../../../lamp-os/src/components/network/mesh/hello_interval.hpp"

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

int8_t rssiOf(wisp::LampInventory& inv) {
  wisp::LampObservation obs[1];
  TEST_ASSERT_EQUAL_size_t(1, inv.copyObservations(obs, 1));
  return obs[0].rssi;
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

// The lamp moved out of direct range: relays keep the entry alive, but the
// direct measurement goes once it is older than the window.
void test_direct_rssi_ages_out_while_relays_arrive(void) {
  wisp::LampInventory inv;
  const uint32_t t0 = 7000;
  hear(inv, 1, -52, t0);
  for (uint32_t t = t0 + wisp::kLampHelloIntervalMs;
       t <= t0 + wisp::kDirectRssiMaxAgeMs; t += wisp::kLampHelloIntervalMs) {
    hear(inv, 1, INT8_MIN, t);
    inv.prune(t, LAMP_PRUNE_TIME_MS);
    TEST_ASSERT_EQUAL_INT8(-52, rssiOf(inv));
  }
  inv.prune(t0 + wisp::kDirectRssiMaxAgeMs + 1, LAMP_PRUNE_TIME_MS);
  TEST_ASSERT_EQUAL_INT8(INT8_MIN, rssiOf(inv));
  TEST_ASSERT_EQUAL_size_t(1, inv.size());

  hear(inv, 1, INT8_MIN, t0 + wisp::kDirectRssiMaxAgeMs + 5000);
  TEST_ASSERT_EQUAL_INT8(INT8_MIN, rssiOf(inv));
  hear(inv, 1, -67, t0 + wisp::kDirectRssiMaxAgeMs + 9000);
  TEST_ASSERT_EQUAL_INT8(-67, rssiOf(inv));
}

// A present lamp that loses two HELLOs in a row is heard directly on the third;
// its measurement never lapses at any prune in between.
void test_direct_rssi_heard_within_window_is_kept(void) {
  wisp::LampInventory inv;
  const int8_t samples[] = {-58, -63, -49, -71};
  uint32_t heardAt = 3300;
  for (int8_t rssi : samples) {
    hear(inv, 1, rssi, heardAt);
    for (uint32_t p = heardAt + 1700; p <= heardAt + wisp::kDirectRssiMaxAgeMs;
         p += 29000) {
      inv.prune(p, LAMP_PRUNE_TIME_MS);
      TEST_ASSERT_EQUAL_INT8(rssi, rssiOf(inv));
    }
    inv.prune(heardAt + wisp::kDirectRssiMaxAgeMs, LAMP_PRUNE_TIME_MS);
    TEST_ASSERT_EQUAL_INT8(rssi, rssiOf(inv));
    heardAt += 3 * wisp::kLampHelloIntervalMs;
  }
}

// The recv task can stamp a HELLO after the loop read nowMs.
void test_direct_rssi_stamped_ahead_of_now_is_kept(void) {
  wisp::LampInventory inv;
  hear(inv, 1, -55, 100000);
  inv.prune(99990, LAMP_PRUNE_TIME_MS);
  TEST_ASSERT_EQUAL_INT8(-55, rssiOf(inv));
}

void test_direct_rssi_ages_across_millis_wrap(void) {
  wisp::LampInventory inv;
  const uint32_t heardAt = UINT32_MAX - 12000;
  hear(inv, 1, -61, heardAt);
  inv.prune(heardAt + 40000, LAMP_PRUNE_TIME_MS);
  TEST_ASSERT_EQUAL_INT8(-61, rssiOf(inv));
  hear(inv, 1, INT8_MIN, heardAt + 60000);
  inv.prune(heardAt + wisp::kDirectRssiMaxAgeMs + 1, LAMP_PRUNE_TIME_MS);
  TEST_ASSERT_EQUAL_INT8(INT8_MIN, rssiOf(inv));
}

void test_direct_rssi_window_derives_from_lamp_hello_interval(void) {
  TEST_ASSERT_EQUAL_UINT32(LAMP_HELLO_INTERVAL_MS, wisp::kLampHelloIntervalMs);
  TEST_ASSERT_TRUE(wisp::kDirectRssiMaxAgeMs >= 3u * LAMP_HELLO_INTERVAL_MS);
  TEST_ASSERT_TRUE(wisp::kDirectRssiMaxAgeMs < LAMP_PRUNE_TIME_MS);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_copy_observations_fills_mac_and_rssi);
  RUN_TEST(test_copy_observations_caps_at_max);
  RUN_TEST(test_record_hello_preserves_rssi_on_unmeasured_update);
  RUN_TEST(test_prune_drops_aged_entries);
  RUN_TEST(test_full_roster_evicts_oldest);
  RUN_TEST(test_direct_rssi_ages_out_while_relays_arrive);
  RUN_TEST(test_direct_rssi_heard_within_window_is_kept);
  RUN_TEST(test_direct_rssi_stamped_ahead_of_now_is_kept);
  RUN_TEST(test_direct_rssi_ages_across_millis_wrap);
  RUN_TEST(test_direct_rssi_window_derives_from_lamp_hello_interval);
  return UNITY_END();
}
