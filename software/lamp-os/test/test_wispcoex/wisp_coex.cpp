// Pins the per-wisp presence accumulator feeding the [wispcoex] LAMP_DEBUG
// line: recv count and maxGapMs (wall-clock between hellos). No seq-gap loss
// math: the wisp shares one seq across all its message types.

#include <unity.h>
#include <cstdint>

#include "components/network/mesh/wisp_coex.hpp"  // -I src on native

using lamp::WispCoexMeter;

void setUp(void) {}
void tearDown(void) {}

void test_meter_accumulates_recv_and_maxgap_across_calls() {
  WispCoexMeter meter;
  const uint8_t mac[6] = {0xEB, 0x64, 0x01, 0x02, 0x03, 0x04};

  lamp::WispCoexSlot& s1 = meter.record(mac, 1000);
  TEST_ASSERT_EQUAL_UINT32(1, s1.recv);
  TEST_ASSERT_EQUAL_UINT32(0, s1.maxGapMs);

  lamp::WispCoexSlot& s2 = meter.record(mac, 3000);
  TEST_ASSERT_EQUAL_UINT32(2, s2.recv);
  TEST_ASSERT_EQUAL_UINT32(2000, s2.maxGapMs);

  lamp::WispCoexSlot& s3 = meter.record(mac, 3100);
  TEST_ASSERT_EQUAL_UINT32(3, s3.recv);
  TEST_ASSERT_EQUAL_UINT32(2000, s3.maxGapMs);
}

void test_meter_tracks_distinct_wisps_independently() {
  WispCoexMeter meter;
  const uint8_t macA[6] = {0xEB, 0x64, 0x01, 0x02, 0x03, 0x04};
  const uint8_t macB[6] = {0xEB, 0x64, 0x0A, 0x0B, 0x0C, 0x0D};

  meter.record(macA, 0);
  meter.record(macA, 100);
  lamp::WispCoexSlot& a = meter.record(macA, 200);
  lamp::WispCoexSlot& b = meter.record(macB, 200);

  TEST_ASSERT_EQUAL_UINT32(3, a.recv);
  TEST_ASSERT_EQUAL_UINT32(100, a.maxGapMs);
  TEST_ASSERT_EQUAL_UINT32(1, b.recv);
  TEST_ASSERT_EQUAL_UINT32(0, b.maxGapMs);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_meter_accumulates_recv_and_maxgap_across_calls);
  RUN_TEST(test_meter_tracks_distinct_wisps_independently);
  return UNITY_END();
}
