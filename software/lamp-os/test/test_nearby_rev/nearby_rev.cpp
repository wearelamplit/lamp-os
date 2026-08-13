// Pins the nearby membership-hash contract that drives stateNotify's
// nearbyRev push: the hash changes when an app-visible roster field changes
// and stays fixed on an rssi-only or lastSeenMs-only refresh of an existing
// peer (those churn at ~30 Hz and must not bump the rev).
//
// Mirror of ble_control.cpp::buildNearbyJson's per-entry fold. Keep in sync:
// the field set here is exactly what the app renders, minus lastSeenMs + rssi.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "components/network/mesh/proximity.hpp"

namespace {

constexpr uint32_t kNow      = 1'000'000;
constexpr uint32_t kMaxAgeMs = 240000;

struct Peer {
  std::string name;
  uint8_t base[4]  = {0, 0, 0, 0};
  uint8_t shade[4] = {0, 0, 0, 0};
  uint8_t mac[6]   = {0, 0, 0, 0, 0, 0};
  bool hasMac = false;
  uint32_t lastSeenNearMs = 0;
  uint32_t lastSeenMeshMs = 0;
  uint32_t firmwareVersion = 0;
  std::string fwChannel;
  uint8_t otaState = 0;
  uint8_t otaSendingTo[6] = {0, 0, 0, 0, 0, 0};
  bool hasOtaSendingTo = false;
  int8_t lastRssi = -127;   // app-visible but excluded from the hash
};

void fnv1a(uint32_t& h, const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (len--) { h ^= *p++; h *= 16777619u; }
}

uint32_t rosterHash(std::vector<Peer> peers) {
  std::sort(peers.begin(), peers.end(),
            [](const Peer& a, const Peer& b) { return a.name < b.name; });
  uint32_t h = 2166136261u;
  for (const auto& p : peers) {
    const uint8_t flags =
        (p.lastSeenNearMs != 0 ? 1 : 0) | (p.lastSeenMeshMs != 0 ? 2 : 0) |
        (lamp::isNearNow(p.lastSeenNearMs, kNow, kMaxAgeMs) ? 4 : 0) |
        (p.hasMac ? 8 : 0) | (p.hasOtaSendingTo ? 16 : 0);
    fnv1a(h, p.name.data(), p.name.size());
    fnv1a(h, &flags, 1);
    if (p.hasMac) fnv1a(h, p.mac, 6);
    fnv1a(h, p.shade, 4);
    fnv1a(h, p.base, 4);
    fnv1a(h, &p.firmwareVersion, sizeof(p.firmwareVersion));
    fnv1a(h, p.fwChannel.data(), p.fwChannel.size());
    fnv1a(h, &p.otaState, 1);
    if (p.hasOtaSendingTo) fnv1a(h, p.otaSendingTo, 6);
  }
  return h;
}

Peer nearPeer(const std::string& name) {
  Peer p;
  p.name = name;
  p.lastSeenNearMs = kNow - 1000;  // recent -> near
  p.lastSeenMeshMs = 0;
  p.lastRssi = -60;
  p.fwChannel = "standard-beta";
  return p;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_peer_added_changes_hash() {
  std::vector<Peer> a = {nearPeer("jacko")};
  std::vector<Peer> b = {nearPeer("jacko"), nearPeer("flora")};
  TEST_ASSERT_NOT_EQUAL(rosterHash(a), rosterHash(b));
}

void test_rename_changes_hash() {
  std::vector<Peer> a = {nearPeer("jacko")};
  std::vector<Peer> b = {nearPeer("jackO")};
  TEST_ASSERT_NOT_EQUAL(rosterHash(a), rosterHash(b));
}

void test_color_change_changes_hash() {
  std::vector<Peer> a = {nearPeer("jacko")};
  std::vector<Peer> b = a;
  b[0].base[0] = 0xFF;
  TEST_ASSERT_NOT_EQUAL(rosterHash(a), rosterHash(b));
}

void test_via_transition_changes_hash() {
  std::vector<Peer> a = {nearPeer("jacko")};  // BLE only
  std::vector<Peer> b = a;
  b[0].lastSeenMeshMs = kNow - 500;  // now also via ESP-NOW
  TEST_ASSERT_NOT_EQUAL(rosterHash(a), rosterHash(b));
}

void test_ota_state_change_changes_hash() {
  std::vector<Peer> a = {nearPeer("jacko")};
  std::vector<Peer> b = a;
  b[0].otaState = 2;
  TEST_ASSERT_NOT_EQUAL(rosterHash(a), rosterHash(b));
}

void test_rssi_only_refresh_does_not_change_hash() {
  std::vector<Peer> a = {nearPeer("jacko")};
  std::vector<Peer> b = a;
  b[0].lastRssi = -80;
  TEST_ASSERT_EQUAL(rosterHash(a), rosterHash(b));
}

void test_last_seen_only_refresh_does_not_change_hash() {
  // A newer near timestamp (still within the prune window) must not move the
  // hash: viaBle and near are both unchanged.
  std::vector<Peer> a = {nearPeer("jacko")};
  std::vector<Peer> b = a;
  b[0].lastSeenNearMs = kNow - 10;
  TEST_ASSERT_EQUAL(rosterHash(a), rosterHash(b));
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_peer_added_changes_hash);
  RUN_TEST(test_rename_changes_hash);
  RUN_TEST(test_color_change_changes_hash);
  RUN_TEST(test_via_transition_changes_hash);
  RUN_TEST(test_ota_state_change_changes_hash);
  RUN_TEST(test_rssi_only_refresh_does_not_change_hash);
  RUN_TEST(test_last_seen_only_refresh_does_not_change_hash);
  return UNITY_END();
}
