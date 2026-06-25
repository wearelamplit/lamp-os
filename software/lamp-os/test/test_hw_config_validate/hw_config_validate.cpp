// software/lamp-os/test/test_hw_config_validate/hw_config_validate.cpp
//
// Verifies validateHwConfig() detects malformed configs. Native test —
// validates the predicate only, not the FATAL halt path (which needs
// Arduino).

#include <unity.h>
#include "core/hw_config.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_empty_surfaces_invalid() {
  lamp::HwConfig hw{};
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_duplicate_pins_invalid() {
  lamp::HwConfig hw{};
  hw.surfaces.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW});
  hw.surfaces.push_back({lamp::Surface::Base,  12, lamp::ByteOrder::GRBW});
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_well_formed_config_valid() {
  lamp::HwConfig hw{};
  hw.surfaces.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW});
  hw.surfaces.push_back({lamp::Surface::Base,  14, lamp::ByteOrder::GRBW});
  TEST_ASSERT_TRUE(lamp::validateHwConfig(hw));
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_empty_surfaces_invalid);
  RUN_TEST(test_duplicate_pins_invalid);
  RUN_TEST(test_well_formed_config_valid);
  return UNITY_END();
}
