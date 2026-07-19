#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "util/bd_addr.hpp"

// Keep in sync with the per-peer lampId emit in
// ble_control.cpp::buildNearbyJson.
std::string emitNearbyPeerSnippet(bool hasMac, const uint8_t mac[6]) {
  std::string out = "{";
  if (hasMac) {
    char buf[18];
    lamp::formatBdAddr(mac, buf);
    out += "\"lampId\":\"";
    out += buf;
    out += '"';
  }
  out += '}';
  return out;
}

void setUp(void) {}
void tearDown(void) {}

void test_nearby_emits_lampId_not_bdAddr() {
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  std::string json = emitNearbyPeerSnippet(true, mac);
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"lampId\":\"C4:DD:57:EB:64:60\""));
  TEST_ASSERT_NULL(strstr(json.c_str(), "\"bdAddr\""));
  TEST_ASSERT_NULL(strstr(json.c_str(), "\"mac\":"));
}

void test_nearby_omits_lampId_without_mac() {
  const uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  std::string json = emitNearbyPeerSnippet(false, mac);
  TEST_ASSERT_EQUAL_STRING("{}", json.c_str());
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_nearby_emits_lampId_not_bdAddr);
  RUN_TEST(test_nearby_omits_lampId_without_mac);
  return UNITY_END();
}
