// Native-host test: snafu boots with the roles/segments hardware map.
//
// snafu_lamp.hpp can't compile natively (pulls the full framework), so the
// HwConfig it declares is mirrored here and checked against the real
// validateHwConfig (src/core/hw_config.hpp is Arduino-free). Keep in sync with
// src/lamps/snafu/snafu_lamp.hpp.
//   Shade role: Small Dots(16,pin14) + Medium(12,pin27) + Big(9,pin26).
//   Base role:  Stem(24,pin12), the broadcast segment.

#include <unity.h>

#include "core/hw_config.hpp"

using namespace lamp;

static HwConfig snafuHwConfig() {
  return HwConfig{
    .strips = {
      {.role=Surface::Shade, .pin=14, .byteOrder=ByteOrder::GRBW, .pixelCount=16, .name="Small Dots"},
      {.role=Surface::Shade, .pin=27, .byteOrder=ByteOrder::GRBW, .pixelCount=12, .name="Medium Dots"},
      {.role=Surface::Shade, .pin=26, .byteOrder=ByteOrder::GRBW, .pixelCount=9,  .name="Big Dots"},
      {.role=Surface::Base,  .pin=12, .byteOrder=ByteOrder::GRBW, .pixelCount=24, .name="Stem", .broadcast=1},
    },
    .maxBrightness = 180,
  };
}

void setUp() {}
void tearDown() {}

void test_snafu_map_is_valid() {
  TEST_ASSERT_TRUE(validateHwConfig(snafuHwConfig()));
}

void test_snafu_role_geometry() {
  HwConfig hw = snafuHwConfig();
  uint16_t shadePx = 0, basePx = 0;
  int shadeSegs = 0, baseSegs = 0, broadcast = 0;
  for (const auto& s : hw.strips) {
    if (s.role == Surface::Shade) { shadePx += s.pixelCount; shadeSegs++; }
    else { basePx += s.pixelCount; baseSegs++; }
    if (s.broadcast) broadcast++;
  }
  TEST_ASSERT_EQUAL_INT(3, shadeSegs);
  TEST_ASSERT_EQUAL_INT(1, baseSegs);
  TEST_ASSERT_EQUAL_UINT16(37, shadePx);   // 16 + 12 + 9
  TEST_ASSERT_EQUAL_UINT16(24, basePx);
  TEST_ASSERT_EQUAL_INT(1, broadcast);     // exactly one (the Stem)
}

// A dup-pin or missing-role map must fail the fatal gate.
void test_dup_pin_rejected() {
  HwConfig hw = snafuHwConfig();
  hw.strips[1].pin = 14;  // collide Medium Dots with Small Dots
  TEST_ASSERT_FALSE(validateHwConfig(hw));
}

void test_missing_base_rejected() {
  HwConfig hw = snafuHwConfig();
  hw.strips.pop_back();  // drop the only Base strip
  TEST_ASSERT_FALSE(validateHwConfig(hw));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_snafu_map_is_valid);
  RUN_TEST(test_snafu_role_geometry);
  RUN_TEST(test_dup_pin_rejected);
  RUN_TEST(test_missing_base_rejected);
  return UNITY_END();
}
