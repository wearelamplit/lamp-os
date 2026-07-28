// Pins the WISP_HELLO seq-gap math: wraparound-safe missed-count and the
// per-wisp accumulator that feeds the [wispcoex] LAMP_DEBUG line.

#include <unity.h>
#include <cstdint>

#include "components/network/mesh/wisp_coex.hpp"  // -I src on native

using lamp::wispCoexMissed;
using lamp::WispCoexMeter;

void setUp(void) {}
void tearDown(void) {}

void test_consecutive_seq_is_no_miss() {
  TEST_ASSERT_EQUAL_UINT16(0, wispCoexMissed(10, 11));
}

void test_gap_of_n_counts_the_skipped_seqs() {
  // 10 seen, 15 arrives: 11,12,13,14 missed.
  TEST_ASSERT_EQUAL_UINT16(4, wispCoexMissed(10, 15));
}

void test_wraparound_is_uint16_safe() {
  TEST_ASSERT_EQUAL_UINT16(2, wispCoexMissed(65534, 1));
}

void test_duplicate_retransmit_is_zero_missed() {
  TEST_ASSERT_EQUAL_UINT16(0, wispCoexMissed(42, 42));
}

void test_implausible_jump_is_treated_as_resync() {
  TEST_ASSERT_EQUAL_UINT16(0, wispCoexMissed(500, 2000));
}

void test_meter_accumulates_recv_and_missed_across_calls() {
  WispCoexMeter meter;
  const uint8_t mac[6] = {0xEB, 0x64, 0x01, 0x02, 0x03, 0x04};

  lamp::WispCoexSlot& s1 = meter.record(mac, 1, 1000);
  TEST_ASSERT_EQUAL_UINT32(1, s1.recv);
  TEST_ASSERT_EQUAL_UINT32(0, s1.missed);

  lamp::WispCoexSlot& s2 = meter.record(mac, 5, 3000);
  TEST_ASSERT_EQUAL_UINT32(2, s2.recv);
  TEST_ASSERT_EQUAL_UINT32(3, s2.missed);
  TEST_ASSERT_EQUAL_UINT32(2000, s2.maxGapMs);

  lamp::WispCoexSlot& s3 = meter.record(mac, 6, 3100);
  TEST_ASSERT_EQUAL_UINT32(3, s3.recv);
  TEST_ASSERT_EQUAL_UINT32(3, s3.missed);
  TEST_ASSERT_EQUAL_UINT32(2000, s3.maxGapMs);
}

void test_meter_tracks_distinct_wisps_independently() {
  WispCoexMeter meter;
  const uint8_t macA[6] = {0xEB, 0x64, 0x01, 0x02, 0x03, 0x04};
  const uint8_t macB[6] = {0xEB, 0x64, 0x0A, 0x0B, 0x0C, 0x0D};

  meter.record(macA, 1, 0);
  meter.record(macA, 2, 100);
  lamp::WispCoexSlot& a = meter.record(macA, 3, 200);
  lamp::WispCoexSlot& b = meter.record(macB, 50, 200);

  TEST_ASSERT_EQUAL_UINT32(3, a.recv);
  TEST_ASSERT_EQUAL_UINT32(0, a.missed);
  TEST_ASSERT_EQUAL_UINT32(1, b.recv);
  TEST_ASSERT_EQUAL_UINT32(0, b.missed);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_consecutive_seq_is_no_miss);
  RUN_TEST(test_gap_of_n_counts_the_skipped_seqs);
  RUN_TEST(test_wraparound_is_uint16_safe);
  RUN_TEST(test_duplicate_retransmit_is_zero_missed);
  RUN_TEST(test_implausible_jump_is_treated_as_resync);
  RUN_TEST(test_meter_accumulates_recv_and_missed_across_calls);
  RUN_TEST(test_meter_tracks_distinct_wisps_independently);
  return UNITY_END();
}
