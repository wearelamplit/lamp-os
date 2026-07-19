// Native-host unit tests pinning the roster's single-source RSSI contract
// and mac fill rules.
//
// lastRssi is written only by the BLE scan path (~30 Hz) so the
// RSSI-descending sort key never compares values from two different radio
// transports (which have different absolute readings on the same physical
// link).
//
// The inline mirror captures the invariants:
//   - BLE scan with a real RSSI updates lastRssi on insert + on update.
//   - BLE scan with the -127 sentinel does NOT clobber a stored value.
//   - ESP-NOW HELLO never touches lastRssi (regardless of the rssi arg).
//   - ESP-NOW stores the raw mesh mac and always wins.
//   - BLE scan recovers the mac from its scanned address (address - 2),
//     but only when no mac is set yet.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "util/bd_addr.hpp"

namespace lamp {

struct RosterEntry {
  std::string name;
  int8_t lastRssi = -127;
  uint8_t mac[6] = {0};
  bool hasMac = false;

  std::string macStr() const {
    if (!hasMac) return {};
    char buf[18];
    formatBdAddr(mac, buf);
    return buf;
  }
};

class LampRoster {
 public:
  // Mirror of addOrUpdateFromBle: writes lastRssi on insert
  // unconditionally; on update only if rssi != -127. Recovers the mac
  // from the scanned BLE address (address - 2) only when no mac is set
  // (the ESP-NOW real mac wins).
  void addOrUpdateFromBle(const std::string& name, int8_t rssi,
                          const std::string& bleAddr = "") {
    uint8_t ble[6];
    const bool bleOk = parseBdAddr(bleAddr.c_str(), ble);
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const RosterEntry& p) { return p.name == name; });
    if (it == store_.end()) {
      RosterEntry e;
      e.name = name;
      e.lastRssi = rssi;
      if (bleOk) {
        meshMacFromBleAddr(ble, e.mac);
        e.hasMac = true;
      }
      store_.push_back(e);
    } else {
      if (rssi != -127) it->lastRssi = rssi;
      if (!it->hasMac && bleOk) {
        meshMacFromBleAddr(ble, it->mac);
        it->hasMac = true;
      }
    }
  }

  // Mirror of addOrUpdateFromEspNow: does NOT touch lastRssi. Stores the
  // raw mesh mac and always wins over a BLE-derived mac.
  void addOrUpdateFromEspNow(const std::string& name, int8_t /*rssi*/,
                             const uint8_t mac[6] = nullptr) {
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const RosterEntry& p) { return p.name == name; });
    if (it == store_.end()) {
      RosterEntry e;
      e.name = name;
      if (mac != nullptr) {
        std::memcpy(e.mac, mac, 6);
        e.hasMac = true;
      }
      store_.push_back(e);
    } else {
      if (mac != nullptr) {
        std::memcpy(it->mac, mac, 6);
        it->hasMac = true;
      }
    }
  }

  int8_t lastRssiOf(const std::string& name) const {
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const RosterEntry& p) { return p.name == name; });
    return it == store_.end() ? -127 : it->lastRssi;
  }

  std::string macOf(const std::string& name) const {
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const RosterEntry& p) { return p.name == name; });
    return it == store_.end() ? std::string{} : it->macStr();
  }

  bool findByMac(const uint8_t mac[6], RosterEntry& out) const {
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const RosterEntry& p) {
                             return p.hasMac && std::memcmp(p.mac, mac, 6) == 0;
                           });
    if (it == store_.end()) return false;
    out = *it;
    return true;
  }

  size_t size() const { return store_.size(); }

 private:
  std::vector<RosterEntry> store_;
};

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

void test_ble_scan_sets_last_rssi_on_insert() {
  lamp::LampRoster lamps;
  lamps.addOrUpdateFromBle("jacko", -72);
  TEST_ASSERT_EQUAL_INT8(-72, lamps.lastRssiOf("jacko"));
}

void test_ble_scan_updates_last_rssi_on_existing_entry() {
  lamp::LampRoster lamps;
  lamps.addOrUpdateFromBle("jacko", -72);
  lamps.addOrUpdateFromBle("jacko", -75);
  TEST_ASSERT_EQUAL_INT8(-75, lamps.lastRssiOf("jacko"));
}

void test_ble_scan_sentinel_does_not_clobber_existing_value() {
  // The -127 sentinel means "no reading" and must NOT overwrite a real
  // stored value.
  lamp::LampRoster lamps;
  lamps.addOrUpdateFromBle("jacko", -72);
  lamps.addOrUpdateFromBle("jacko", -127);  // sentinel
  TEST_ASSERT_EQUAL_INT8(-72, lamps.lastRssiOf("jacko"));
}

void test_esp_now_does_not_write_last_rssi() {
  // ESP-NOW HELLO does NOT touch lastRssi. Even with a real rssi
  // argument, the stored value must remain whatever BLE scan put there
  // (or -127 if no BLE sighting yet).
  lamp::LampRoster lamps;
  lamps.addOrUpdateFromBle("jacko", -72);
  TEST_ASSERT_EQUAL_INT8(-72, lamps.lastRssiOf("jacko"));
  lamps.addOrUpdateFromEspNow("jacko", -65);
  TEST_ASSERT_EQUAL_INT8(-72, lamps.lastRssiOf("jacko"));
}

void test_esp_now_only_peer_has_sentinel_last_rssi() {
  // A peer that ONLY appears via ESP-NOW (never via BLE scan) has no
  // RSSI source — lastRssi stays at the -127 default.
  lamp::LampRoster lamps;
  lamps.addOrUpdateFromEspNow("phantom", -65);
  TEST_ASSERT_EQUAL_INT8(-127, lamps.lastRssiOf("phantom"));
}

void test_roster_identity_is_raw_mac() {
  // A mesh sighting is found by its raw mac; a BLE sighting recovers the
  // mac as (scanned address - 2).
  lamp::LampRoster roster;
  uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  roster.addOrUpdateFromEspNow("flora", -50, mac);
  lamp::RosterEntry out;
  TEST_ASSERT_TRUE(roster.findByMac(mac, out));
  TEST_ASSERT_EQUAL_STRING("C4:DD:57:EB:64:60", out.macStr().c_str());

  roster.addOrUpdateFromBle("gramp", -40, "C4:DD:57:EB:64:62");
  uint8_t derived[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  lamp::RosterEntry out2;
  TEST_ASSERT_TRUE(roster.findByMac(derived, out2));
}

void test_esp_now_stores_raw_mac() {
  // A peer first heard over ESP-NOW stores the raw mesh mac verbatim.
  lamp::LampRoster lamps;
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  lamps.addOrUpdateFromEspNow("phantom", -65, mac);
  TEST_ASSERT_EQUAL_STRING("C4:DD:57:EB:64:60", lamps.macOf("phantom").c_str());
}

void test_esp_now_real_mac_wins_over_ble_derived() {
  // The ESP-NOW mac is the source of truth. A mac recovered from a BLE
  // scan is overwritten by the real mac from a later HELLO.
  lamp::LampRoster lamps;
  lamps.addOrUpdateFromBle("jacko", -72, "AA:BB:CC:DD:EE:FF");
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  lamps.addOrUpdateFromEspNow("jacko", -65, mac);
  TEST_ASSERT_EQUAL_STRING("C4:DD:57:EB:64:60", lamps.macOf("jacko").c_str());
}

void test_ble_derives_mac_minus_two() {
  // A BLE-only sighting recovers the mesh mac as (scanned address - 2);
  // NimBLEAddress::toString() is lowercase, formatted back canonical.
  lamp::LampRoster lamps;
  lamps.addOrUpdateFromBle("jacko", -72, "aa:bb:cc:dd:ee:ff");
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FD", lamps.macOf("jacko").c_str());
}

void test_ble_derived_mac_does_not_overwrite_real_mac() {
  // ESP-NOW sets the real mac first; a later BLE sighting for the same
  // name whose (address - 2) differs must NOT clobber it (the !hasMac
  // guard). Fails if that guard is removed from the update branch.
  lamp::LampRoster lamps;
  const uint8_t realMac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  lamps.addOrUpdateFromEspNow("flora", -50, realMac);
  lamps.addOrUpdateFromBle("flora", -40, "AA:BB:CC:DD:EE:FF");  // -2 = AA:..:FD
  lamp::RosterEntry out;
  TEST_ASSERT_TRUE(lamps.findByMac(realMac, out));
  TEST_ASSERT_EQUAL_STRING("C4:DD:57:EB:64:60", lamps.macOf("flora").c_str());
}

void test_ble_derives_mac_borrow_across_byte() {
  // Last byte < 2 forces the subtract-2 borrow into the prior byte:
  // 0x01 - 2 = 0xFF, carry -1 into 0xEE -> 0xED.
  lamp::LampRoster lamps;
  lamps.addOrUpdateFromBle("jacko", -72, "AA:BB:CC:DD:EE:01");
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:ED:FF", lamps.macOf("jacko").c_str());
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
  RUN_TEST(test_roster_identity_is_raw_mac);
  RUN_TEST(test_esp_now_stores_raw_mac);
  RUN_TEST(test_esp_now_real_mac_wins_over_ble_derived);
  RUN_TEST(test_ble_derives_mac_minus_two);
  RUN_TEST(test_ble_derived_mac_does_not_overwrite_real_mac);
  RUN_TEST(test_ble_derives_mac_borrow_across_byte);

  return UNITY_END();
}
