// Native-host unit tests for the Proximity classifier.
//
// Context: lamps are categorized into three coarse distance buckets
// (Near/Around/Far) based on BLE-scan RSSI. The categorization is
// used by:
//   - the lamp-emitted nearby JSON section (for the app's social tab
//     proximity labels — single source of truth for thresholds)
//   - the optional NearbyLamps::getPeersInBucket query (for future
//     "do X to lamps in bucket Y" firmware behaviors)
//
// Thresholds are bench-calibrated against real bench data (2026-06-17):
//   jacko at 5" or other at 10' (both -72 dBm) → Near
//   oatmilky in next room through wall (-84 dBm) → Around
//   vamp at far end of living room (-97 dBm) → Far
//
// Zero-dependency: the header includes only <cstdint>, so the predicate
// links cleanly into both the production firmware build and this native
// test env.

#include <unity.h>

#include "util/proximity.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_near_bucket() {
  // >= -80 dBm → Near
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Near),
                        static_cast<int>(lamp::proximityFor(-50)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Near),
                        static_cast<int>(lamp::proximityFor(-72)));  // jacko, other
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Near),
                        static_cast<int>(lamp::proximityFor(-80)));  // boundary
}

void test_around_bucket() {
  // -89..-81 dBm → Around (-80 is Near, -90 is Around — boundary semantics)
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Around),
                        static_cast<int>(lamp::proximityFor(-81)));  // just below near boundary
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Around),
                        static_cast<int>(lamp::proximityFor(-84)));  // oatmilky
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Around),
                        static_cast<int>(lamp::proximityFor(-89)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Around),
                        static_cast<int>(lamp::proximityFor(-90)));  // boundary
}

void test_far_bucket() {
  // < -90 dBm → Far
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Far),
                        static_cast<int>(lamp::proximityFor(-91)));  // just below around boundary
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Far),
                        static_cast<int>(lamp::proximityFor(-97)));  // vamp
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lamp::Proximity::Far),
                        static_cast<int>(lamp::proximityFor(-127)));  // -127 sentinel
}

void test_proximityToInt_matches_enum() {
  // The to-int conversion must round-trip the enum order so the JSON
  // emit + app-side render agree on which bucket is which.
  TEST_ASSERT_EQUAL_UINT8(0, lamp::proximityToInt(lamp::Proximity::Near));
  TEST_ASSERT_EQUAL_UINT8(1, lamp::proximityToInt(lamp::Proximity::Around));
  TEST_ASSERT_EQUAL_UINT8(2, lamp::proximityToInt(lamp::Proximity::Far));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_near_bucket);
  RUN_TEST(test_around_bucket);
  RUN_TEST(test_far_bucket);
  RUN_TEST(test_proximityToInt_matches_enum);

  return UNITY_END();
}
