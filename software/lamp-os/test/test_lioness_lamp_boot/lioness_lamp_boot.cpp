// Native-host test: Lioness's declared hardware map is valid.
// lioness_lamp.hpp pulls the full framework, so mirror its HwConfig here and
// check it against the Arduino-free validateHwConfig. Keep in sync with
// src/lamps/lioness/lioness_lamp.hpp.
//   Shade: pin14, 36px. Base: Main(pin4,32px,broadcast) + Lions(pin5,18px).

#include <string>

#include <unity.h>
#include "core/hw_config.hpp"

using namespace lamp;

static HwConfig lionessHwConfig() {
  return HwConfig{
    .strips = {
      {.role=Surface::Shade, .pin=14, .byteOrder=ByteOrder::GRBW, .pixelCount=36, .name="Shade"},
      {.role=Surface::Base,  .pin=4,  .byteOrder=ByteOrder::GRBW, .pixelCount=32, .name="Main", .broadcast=1},
      {.role=Surface::Base,  .pin=5,  .byteOrder=ByteOrder::GRBW, .pixelCount=18, .name="Lions"},
    },
    .maxBrightness = 230,
    .supplyBudgetMa = 1400,
  };
}

void setUp() {}
void tearDown() {}

void test_lioness_map_is_valid() {
  TEST_ASSERT_TRUE(validateHwConfig(lionessHwConfig()));
}

void test_lioness_role_geometry() {
  HwConfig hw = lionessHwConfig();
  uint16_t shadePx = 0, basePx = 0;
  int shadeSegs = 0, baseSegs = 0, broadcast = 0;
  for (const auto& s : hw.strips) {
    if (s.role == Surface::Shade) { shadePx += s.pixelCount; shadeSegs++; }
    else { basePx += s.pixelCount; baseSegs++; }
    if (s.broadcast) broadcast++;
  }
  TEST_ASSERT_EQUAL_INT(1, shadeSegs);
  TEST_ASSERT_EQUAL_INT(2, baseSegs);          // Main + Lions
  TEST_ASSERT_EQUAL_UINT16(36, shadePx);
  TEST_ASSERT_EQUAL_UINT16(50, basePx);        // 32 + 18
  TEST_ASSERT_EQUAL_INT(1, broadcast);         // exactly one (Main)
}

void test_lioness_lions_is_18px() {
  HwConfig hw = lionessHwConfig();
  bool found = false;
  for (const auto& s : hw.strips) {
    if (std::string(s.name) == "Lions") { found = true; TEST_ASSERT_EQUAL_UINT16(18, s.pixelCount); }
  }
  TEST_ASSERT_TRUE(found);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lioness_map_is_valid);
  RUN_TEST(test_lioness_role_geometry);
  RUN_TEST(test_lioness_lions_is_18px);
  return UNITY_END();
}
