// Native-host test: Loaf's declared hardware map is valid.
// loaf_lamp.hpp pulls the full framework, so mirror its HwConfig here and
// check it against the Arduino-free validateHwConfig. Keep in sync with
// src/lamps/loaf/loaf_lamp.hpp.
//   Base: pin14, 40px, broadcast. Shade: pin12, 12px.

#include <string>

#include <unity.h>
#include "core/hw_config.hpp"

using namespace lamp;

static HwConfig loafHwConfig() {
  return HwConfig{
    .strips = {
      {.role=Surface::Base,  .pin=14, .byteOrder=ByteOrder::GRBW, .pixelCount=40, .name="Base", .broadcast=1},
      {.role=Surface::Shade, .pin=12, .byteOrder=ByteOrder::GRBW, .pixelCount=12, .name="Shade"},
    },
    .maxBrightness = 230,
    .supplyBudgetMa = 1400,
  };
}

void setUp() {}
void tearDown() {}

void test_loaf_map_is_valid() {
  TEST_ASSERT_TRUE(validateHwConfig(loafHwConfig()));
}

void test_loaf_role_geometry() {
  HwConfig hw = loafHwConfig();
  uint16_t shadePx = 0, basePx = 0;
  int shadeSegs = 0, baseSegs = 0, broadcast = 0;
  for (const auto& s : hw.strips) {
    if (s.role == Surface::Shade) { shadePx += s.pixelCount; shadeSegs++; }
    else { basePx += s.pixelCount; baseSegs++; }
    if (s.broadcast) broadcast++;
  }
  TEST_ASSERT_EQUAL_INT(1, shadeSegs);
  TEST_ASSERT_EQUAL_INT(1, baseSegs);
  TEST_ASSERT_EQUAL_UINT16(12, shadePx);
  TEST_ASSERT_EQUAL_UINT16(40, basePx);
  TEST_ASSERT_EQUAL_INT(1, broadcast);         // exactly one (Base)
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_loaf_map_is_valid);
  RUN_TEST(test_loaf_role_geometry);
  return UNITY_END();
}
