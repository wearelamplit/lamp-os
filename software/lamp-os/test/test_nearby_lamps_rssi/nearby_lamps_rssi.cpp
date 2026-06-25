// Native-host unit tests pinning the Phase D single-source RSSI contract.
//
// Context: lastRssi was previously written on every ESP-NOW HELLO frame
// (~0.2 Hz). Phase D moves the write to the BLE scan callback (~30 Hz)
// so PersonalityEngine's closest-Smitten hysteresis check doesn't have
// to compare values from two different radio transports (which have
// different absolute readings on the same physical link).
//
// The inline mirror captures the invariants we care about:
//   - BLE scan with a real RSSI updates lastRssi on insert + on update.
//   - BLE scan with the -127 sentinel does NOT clobber a stored value.
//   - ESP-NOW HELLO never touches lastRssi (regardless of the rssi arg).

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

namespace lamp {

struct NearbyLamp {
  std::string name;
  int8_t lastRssi = -127;
  uint32_t firstSeenMs = 0;
  std::string bdAddr;
};

// Mirror of the same helper in nearby_lamps.cpp — ESP-NOW MAC + 2 on the
// last byte yields the ESP32's BLE BD_ADDR (silicon convention; holds for
// every ESP32 family chip in the fleet).
inline std::string deriveBdAddrFromEspNowMac(const uint8_t mac[6]) {
  char buf[18];
  std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4],
                static_cast<uint8_t>(mac[5] + 2));
  return std::string(buf);
}

class NearbyLamps {
 public:
  // Mirror of Phase D's addOrUpdateFromBle: writes lastRssi on insert
  // unconditionally; on update only if rssi != -127. Phase G.5 adds:
  // stamps firstSeenMs on insert only (never updates on subsequent sightings).
  void addOrUpdateFromBle(const std::string& name, int8_t rssi,
                          const std::string& bdAddr = "") {
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const NearbyLamp& p) { return p.name == name; });
    if (it == store_.end()) {
      NearbyLamp e;
      e.name = name;
      e.lastRssi = rssi;
      e.firstSeenMs = 12345;  // mock timestamp for test
      e.bdAddr = bdAddr;
      store_.push_back(e);
    } else {
      if (rssi != -127) it->lastRssi = rssi;
      if (it->bdAddr.empty()) it->bdAddr = bdAddr;
      // intentionally NOT updating firstSeenMs — Phase G.5 contract
    }
  }

  // Mirror of Phase D's addOrUpdateFromEspNow: does NOT touch lastRssi.
  // Derives bdAddr from the ESP-NOW MAC (silicon +2 rule); existing
  // bdAddr wins over the derivation.
  void addOrUpdateFromEspNow(const std::string& name, int8_t /*rssi*/,
                             const uint8_t mac[6] = nullptr) {
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const NearbyLamp& p) { return p.name == name; });
    const std::string derived =
        mac != nullptr ? deriveBdAddrFromEspNowMac(mac) : std::string();
    if (it == store_.end()) {
      NearbyLamp e;
      e.name = name;
      e.bdAddr = derived;
      // intentionally NOT setting lastRssi — Phase D single-source rule
      store_.push_back(e);
    } else {
      if (it->bdAddr.empty()) it->bdAddr = derived;
    }
    // intentionally no rssi write on update either
  }

  int8_t lastRssiOf(const std::string& name) const {
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const NearbyLamp& p) { return p.name == name; });
    return it == store_.end() ? -127 : it->lastRssi;
  }

  uint32_t firstSeenMsOf(const std::string& name) const {
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const NearbyLamp& p) { return p.name == name; });
    return it == store_.end() ? 0 : it->firstSeenMs;
  }

  std::string bdAddrOf(const std::string& name) const {
    auto it = std::find_if(store_.begin(), store_.end(),
                           [&](const NearbyLamp& p) { return p.name == name; });
    return it == store_.end() ? std::string() : it->bdAddr;
  }

  size_t size() const { return store_.size(); }

 private:
  std::vector<NearbyLamp> store_;
};

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

void test_ble_scan_sets_last_rssi_on_insert() {
  lamp::NearbyLamps lamps;
  lamps.addOrUpdateFromBle("jacko", -72);
  TEST_ASSERT_EQUAL_INT8(-72, lamps.lastRssiOf("jacko"));
}

void test_ble_scan_updates_last_rssi_on_existing_entry() {
  lamp::NearbyLamps lamps;
  lamps.addOrUpdateFromBle("jacko", -72);
  lamps.addOrUpdateFromBle("jacko", -75);
  TEST_ASSERT_EQUAL_INT8(-75, lamps.lastRssiOf("jacko"));
}

void test_ble_scan_sentinel_does_not_clobber_existing_value() {
  // The -127 sentinel means "no reading" and must NOT overwrite a real
  // stored value. Matches the existing conditional-update pattern.
  lamp::NearbyLamps lamps;
  lamps.addOrUpdateFromBle("jacko", -72);
  lamps.addOrUpdateFromBle("jacko", -127);  // sentinel
  TEST_ASSERT_EQUAL_INT8(-72, lamps.lastRssiOf("jacko"));
}

void test_esp_now_does_not_write_last_rssi() {
  // Phase D contract: ESP-NOW HELLO does NOT touch lastRssi. Even with
  // a real rssi argument, the stored value must remain unchanged from
  // whatever BLE scan put there (or -127 if no BLE sighting yet).
  lamp::NearbyLamps lamps;
  // Establish a BLE-source value first.
  lamps.addOrUpdateFromBle("jacko", -72);
  TEST_ASSERT_EQUAL_INT8(-72, lamps.lastRssiOf("jacko"));
  // ESP-NOW HELLO arrives with a different rssi reading — but Phase D
  // says it shouldn't touch lastRssi. Verify.
  lamps.addOrUpdateFromEspNow("jacko", -65);
  TEST_ASSERT_EQUAL_INT8(-72, lamps.lastRssiOf("jacko"));
}

void test_esp_now_only_peer_has_sentinel_last_rssi() {
  // A peer that ONLY appears via ESP-NOW (never via BLE scan) has no
  // RSSI source — lastRssi stays at the -127 default. Today this is
  // the "BLE-deaf" / pre-mesh edge case; in practice every mesh-protocol
  // lamp also broadcasts BLE adverts so this scenario is theoretical.
  lamp::NearbyLamps lamps;
  lamps.addOrUpdateFromEspNow("phantom", -65);
  TEST_ASSERT_EQUAL_INT8(-127, lamps.lastRssiOf("phantom"));
}

void test_esp_now_derives_bd_addr_from_mac_plus_two() {
  // ESP32 silicon convention: BLE BD_ADDR = ESP-NOW (WiFi STA) MAC with
  // +2 on the last byte. A peer first heard over ESP-NOW must populate
  // bdAddr with the derived address so the app's social-tab
  // cross-reference can resolve it without waiting for a BLE adv.
  lamp::NearbyLamps lamps;
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  lamps.addOrUpdateFromEspNow("phantom", -65, mac);
  TEST_ASSERT_EQUAL_STRING("C4:DD:57:EB:64:62",
                            lamps.bdAddrOf("phantom").c_str());
}

void test_esp_now_does_not_overwrite_existing_bd_addr() {
  // The BLE-observed BD_ADDR is the source of truth. Even though the
  // derivation usually agrees, stay conservative: a previously-set
  // bdAddr (from any addOrUpdateFromBle call) must NOT be clobbered by
  // a later ESP-NOW HELLO for the same peer name.
  lamp::NearbyLamps lamps;
  lamps.addOrUpdateFromBle("jacko", -72, "AA:BB:CC:DD:EE:FF");
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  lamps.addOrUpdateFromEspNow("jacko", -65, mac);
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF",
                            lamps.bdAddrOf("jacko").c_str());
}

void test_first_seen_ms_stamped_on_first_sighting_and_stable() {
  // Phase G.5: firstSeenMs is stamped once on the FIRST addOrUpdateFromBle
  // for a given peer. Subsequent sightings must NOT overwrite it. This
  // enables custom behaviors to detect peer arrivals:
  //   if (peer.firstSeenMs >= lastTickMs_) { /* just arrived */ }
  lamp::NearbyLamps lamps;
  lamps.addOrUpdateFromBle("jacko", -50);
  uint32_t firstSeenMs1 = lamps.firstSeenMsOf("jacko");
  TEST_ASSERT_NOT_EQUAL(0, firstSeenMs1);  // must be stamped non-zero

  // Second sighting of the same peer — firstSeenMs must NOT update.
  lamps.addOrUpdateFromBle("jacko", -55);
  uint32_t firstSeenMs2 = lamps.firstSeenMsOf("jacko");
  TEST_ASSERT_EQUAL_UINT32(firstSeenMs1, firstSeenMs2);
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
  RUN_TEST(test_esp_now_derives_bd_addr_from_mac_plus_two);
  RUN_TEST(test_esp_now_does_not_overwrite_existing_bd_addr);
  RUN_TEST(test_first_seen_ms_stamped_on_first_sighting_and_stable);

  return UNITY_END();
}
