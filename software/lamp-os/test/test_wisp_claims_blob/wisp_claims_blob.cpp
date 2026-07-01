#include <unity.h>
#include <cstdint>
#include "components/network/wisp_claims_addr.hpp"

void setUp() {}
void tearDown() {}

void test_adds_two_simple() {
  const uint8_t mesh[6] = {0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA4};
  uint8_t out[6];
  ble_control::bdAddrFromMeshMac(mesh, out);
  const uint8_t want[6] = {0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA6};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 6);
}

void test_carries_across_octets() {
  const uint8_t mesh[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t out[6];
  ble_control::bdAddrFromMeshMac(mesh, out);
  const uint8_t want[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEF, 0x01};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 6);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_adds_two_simple);
  RUN_TEST(test_carries_across_octets);
  return UNITY_END();
}
