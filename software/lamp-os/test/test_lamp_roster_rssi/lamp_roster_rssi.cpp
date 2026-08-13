// Native-host unit tests for the real LampRoster: MAC-keyed identity plus
// the single-source RSSI contract.
//
// lastRssi is written only by the BLE scan path (~30 Hz) so the
// RSSI-descending sort key never compares values from two different radio
// transports (which have different absolute readings on the same physical
// link). espnowRssi carries the mesh-side reading separately.
//
// Identity keys on the mesh (STA) MAC: a mesh sighting stores it raw, a BLE
// sighting recovers it as (scanned address - 2). Two sightings with the same
// recovered/raw MAC merge; two distinct MACs are two peers regardless of name.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// Native-test seam: include the real .cpp so identity + RSSI rules are
// exercised against the shipped class, not a hand-rolled mirror.
#include "components/network/mesh/lamp_roster.cpp"

using namespace lamp;

namespace {

const Color kNoColor = Color();

int8_t lastRssiByName(LampRoster& r, const std::string& name) {
  for (const auto& e : r.getAll()) {
    if (name == e.name) return e.lastRssi;
  }
  return -127;
}

int8_t espnowRssiByName(LampRoster& r, const std::string& name) {
  for (const auto& e : r.getAll()) {
    if (name == e.name) return e.espnowRssi;
  }
  return -127;
}

std::string macStrByName(LampRoster& r, const std::string& name) {
  for (const auto& e : r.getAll()) {
    if (name == e.name) return e.macStr();
  }
  return {};
}

bool hasUngreetedArrival(LampRoster& r, uint32_t maxAgeMs = 240000) {
  RosterEntry out;
  return r.bestUngreetedArrival(
      maxAgeMs, g_mock_millis,
      [](const RosterEntry&) { return true; }, out);
}

}  // namespace

void setUp(void) { set_mock_millis(100000); }
void tearDown(void) {}

// ---- Single-source RSSI contract ----------------------------------------

void test_ble_scan_sets_last_rssi_on_insert() {
  LampRoster r;
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -72);
  TEST_ASSERT_EQUAL_INT8(-72, lastRssiByName(r, "jacko"));
}

void test_ble_scan_updates_last_rssi_on_existing_entry() {
  LampRoster r;
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -72);
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -75);
  TEST_ASSERT_EQUAL_UINT(1u, r.getAll().size());
  TEST_ASSERT_EQUAL_INT8(-75, lastRssiByName(r, "jacko"));
}

void test_ble_scan_sentinel_does_not_clobber_existing_value() {
  LampRoster r;
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -72);
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -127);
  TEST_ASSERT_EQUAL_INT8(-72, lastRssiByName(r, "jacko"));
}

void test_esp_now_does_not_write_last_rssi() {
  // Same physical lamp: BLE address AA:..:FF recovers MAC AA:..:FD, so a
  // later HELLO carrying that raw MAC merges into the same entry. The HELLO
  // must not touch lastRssi (it lives on espnowRssi instead).
  LampRoster r;
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -72);
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFD};
  r.addOrUpdateFromEspNow("jacko", mac, kNoColor, kNoColor, 0, 0, 0, nullptr,
                          nullptr, false, 0, false, nullptr, false, -65);
  TEST_ASSERT_EQUAL_UINT(1u, r.getAll().size());
  TEST_ASSERT_EQUAL_INT8(-72, lastRssiByName(r, "jacko"));
  TEST_ASSERT_EQUAL_INT8(-65, espnowRssiByName(r, "jacko"));
}

void test_esp_now_only_peer_has_sentinel_last_rssi() {
  LampRoster r;
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  r.addOrUpdateFromEspNow("phantom", mac, kNoColor, kNoColor, 0, 0, 0, nullptr,
                          nullptr, false, 0, false, nullptr, false, -65);
  TEST_ASSERT_EQUAL_INT8(-127, lastRssiByName(r, "phantom"));
}

// ---- MAC identity --------------------------------------------------------

void test_esp_now_stores_raw_mac() {
  LampRoster r;
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  r.addOrUpdateFromEspNow("phantom", mac, kNoColor, kNoColor);
  RosterEntry out;
  TEST_ASSERT_TRUE(r.findByMac(mac, out));
  TEST_ASSERT_EQUAL_STRING("C4:DD:57:EB:64:60", out.macStr().c_str());
}

void test_ble_derives_mac_minus_two() {
  LampRoster r;
  r.addOrUpdateFromBle("jacko", "aa:bb:cc:dd:ee:ff", kNoColor, kNoColor, -72);
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FD", macStrByName(r, "jacko").c_str());
}

void test_ble_derives_mac_borrow_across_byte() {
  LampRoster r;
  r.addOrUpdateFromBle("jacko", "AA:BB:CC:DD:EE:01", kNoColor, kNoColor, -72);
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:ED:FF", macStrByName(r, "jacko").c_str());
}

void test_ble_and_mesh_same_lamp_merge_to_one_entry() {
  // BLE address AA:..:FF recovers MAC AA:..:FD; a HELLO with that raw MAC is
  // the same physical lamp and merges into one entry.
  LampRoster r;
  r.addOrUpdateFromBle("flora", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -40);
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFD};
  r.addOrUpdateFromEspNow("flora", mac, kNoColor, kNoColor);
  TEST_ASSERT_EQUAL_UINT(1u, r.getAll().size());
}

// ---- New MAC-keyed contract ----------------------------------------------

// (a) Two peers, same name, different MAC -> two distinct entries.
void test_same_name_distinct_mac_two_entries() {
  LampRoster r;
  const uint8_t macA[6] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x55};
  const uint8_t macB[6] = {0xBB, 0x11, 0x22, 0x33, 0x44, 0x55};
  r.addOrUpdateFromEspNow("twin", macA, kNoColor, kNoColor);
  r.addOrUpdateFromEspNow("twin", macB, kNoColor, kNoColor);
  TEST_ASSERT_EQUAL_UINT(2u, r.getAll().size());
  RosterEntry out;
  TEST_ASSERT_TRUE(r.findByMac(macA, out));
  TEST_ASSERT_TRUE(r.findByMac(macB, out));
}

// (b) Same MAC, name changes -> one entry, name refreshed, acknowledged
//     preserved -> not re-greeted.
void test_same_mac_rename_updates_in_place_keeps_ack() {
  LampRoster r;
  r.addOrUpdateFromBle("flora", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -40);
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFD};
  r.acknowledge(mac);
  TEST_ASSERT_FALSE(hasUngreetedArrival(r));

  r.addOrUpdateFromBle("renamed", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -40);
  TEST_ASSERT_EQUAL_UINT(1u, r.getAll().size());
  TEST_ASSERT_EQUAL_STRING("renamed", r.getAll()[0].name);
  TEST_ASSERT_FALSE(hasUngreetedArrival(r));
}

// (c) acknowledge keyed by MAC: a rename cannot dodge the acknowledgement.
void test_acknowledge_keyed_by_mac_survives_rename() {
  LampRoster r;
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  r.addOrUpdateFromEspNow("orig", mac, kNoColor, kNoColor);
  r.markNear(mac);
  TEST_ASSERT_TRUE(hasUngreetedArrival(r));
  r.acknowledge(mac);
  TEST_ASSERT_FALSE(hasUngreetedArrival(r));
  // Same MAC arrives advertising a different name; still acknowledged.
  r.addOrUpdateFromEspNow("spoofed", mac, kNoColor, kNoColor);
  r.markNear(mac);
  TEST_ASSERT_FALSE(hasUngreetedArrival(r));
}

// (d) Prune-and-return by MAC re-fires (fresh entry, un-acked).
void test_prune_and_return_refires() {
  LampRoster r;
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  r.addOrUpdateFromEspNow("flora", mac, kNoColor, kNoColor);
  r.markNear(mac);
  r.acknowledge(mac);
  TEST_ASSERT_FALSE(hasUngreetedArrival(r));

  set_mock_millis(100000 + 240000 + 1);
  r.prune(240000);
  TEST_ASSERT_EQUAL_UINT(0u, r.getAll().size());

  r.addOrUpdateFromEspNow("flora", mac, kNoColor, kNoColor);
  r.markNear(mac);
  TEST_ASSERT_TRUE(hasUngreetedArrival(r));
}

// (e) A BLE sighting whose address doesn't parse has no MAC to key on and is
//     dropped (no entry).
void test_unparseable_ble_address_dropped() {
  LampRoster r;
  r.addOrUpdateFromBle("junk", "not-a-mac", kNoColor, kNoColor, -50);
  TEST_ASSERT_EQUAL_UINT(0u, r.getAll().size());
}

// (f) An empty-name HELLO (nameLen==0) must not blank a known display name on
//     the merged entry. BLE stored "flora"; a nameless mesh sighting keeps it.
void test_empty_hello_name_does_not_clobber_known_name() {
  LampRoster r;
  r.addOrUpdateFromBle("flora", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -40);
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFD};
  r.addOrUpdateFromEspNow("", mac, kNoColor, kNoColor);
  TEST_ASSERT_EQUAL_UINT(1u, r.getAll().size());
  TEST_ASSERT_EQUAL_STRING("flora", r.getAll()[0].name);
}

// (g) Mirror of (f) on the BLE update branch: a nameless BLE adv merging onto a
//     mesh-named entry keeps the known name.
void test_empty_ble_name_does_not_clobber_known_name() {
  LampRoster r;
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFD};
  r.addOrUpdateFromEspNow("gramp", mac, kNoColor, kNoColor);
  r.addOrUpdateFromBle("", "AA:BB:CC:DD:EE:FF", kNoColor, kNoColor, -40);
  TEST_ASSERT_EQUAL_UINT(1u, r.getAll().size());
  TEST_ASSERT_EQUAL_STRING("gramp", r.getAll()[0].name);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_ble_scan_sets_last_rssi_on_insert);
  RUN_TEST(test_ble_scan_updates_last_rssi_on_existing_entry);
  RUN_TEST(test_ble_scan_sentinel_does_not_clobber_existing_value);
  RUN_TEST(test_esp_now_does_not_write_last_rssi);
  RUN_TEST(test_esp_now_only_peer_has_sentinel_last_rssi);
  RUN_TEST(test_esp_now_stores_raw_mac);
  RUN_TEST(test_ble_derives_mac_minus_two);
  RUN_TEST(test_ble_derives_mac_borrow_across_byte);
  RUN_TEST(test_ble_and_mesh_same_lamp_merge_to_one_entry);
  RUN_TEST(test_same_name_distinct_mac_two_entries);
  RUN_TEST(test_same_mac_rename_updates_in_place_keeps_ack);
  RUN_TEST(test_acknowledge_keyed_by_mac_survives_rename);
  RUN_TEST(test_prune_and_return_refires);
  RUN_TEST(test_unparseable_ble_address_dropped);
  RUN_TEST(test_empty_hello_name_does_not_clobber_known_name);
  RUN_TEST(test_empty_ble_name_does_not_clobber_known_name);

  return UNITY_END();
}
