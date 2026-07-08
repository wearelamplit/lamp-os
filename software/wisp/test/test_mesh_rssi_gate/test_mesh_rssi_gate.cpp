// Native tests for the HELLO RSSI trust gate in MeshRouter.
//
// Pins the contract that fixes relay-RSSI pollution of claim decisions:
//   - Direct reception (radio src == payload src) records the real RSSI.
//   - Relayed reception (radio src != payload src) records INT8_MIN, the
//     never-measured sentinel recordHello preserves (so a relay can't
//     overwrite an existing direct measurement).
//   - Null radio src (no measurement surfaced) records INT8_MIN.

#include <unity.h>

#include <climits>
#include <cstdint>

#include "net/mesh_router.hpp"

namespace {

const uint8_t kOriginator[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
const uint8_t kRelayer[6]    = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

void test_direct_records_measured_rssi(void) {
  TEST_ASSERT_EQUAL_INT8(
      -42, wisp::helloRssiForRecord(kOriginator, kOriginator, -42));
}

void test_relayed_records_never_measured(void) {
  TEST_ASSERT_EQUAL_INT8(
      INT8_MIN, wisp::helloRssiForRecord(kRelayer, kOriginator, -42));
}

void test_null_radio_src_records_never_measured(void) {
  TEST_ASSERT_EQUAL_INT8(
      INT8_MIN, wisp::helloRssiForRecord(nullptr, kOriginator, -42));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_direct_records_measured_rssi);
  RUN_TEST(test_relayed_records_never_measured);
  RUN_TEST(test_null_radio_src_records_never_measured);
  return UNITY_END();
}
