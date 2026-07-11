// software/lamp-os/test/test_hw_config_validate/hw_config_validate.cpp
//
// Verifies validateHwConfig() detects malformed configs. Native test —
// validates the predicate only, not the FATAL halt path (which needs
// Arduino).

#include <unity.h>
#include "core/hw_config.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_empty_strips_invalid() {
  lamp::HwConfig hw{};
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_duplicate_pins_invalid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW});
  hw.strips.push_back({lamp::Surface::Base,  12, lamp::ByteOrder::GRBW});
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_well_formed_config_valid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW});
  hw.strips.push_back({lamp::Surface::Base,  14, lamp::ByteOrder::GRBW});
  TEST_ASSERT_TRUE(lamp::validateHwConfig(hw));
}

void test_missing_shade_invalid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Base, 14, lamp::ByteOrder::GRBW});
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_missing_base_invalid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW});
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_multi_strip_same_role_valid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 14, lamp::ByteOrder::GRBW, 16});
  hw.strips.push_back({lamp::Surface::Shade, 27, lamp::ByteOrder::GRBW, 12});
  hw.strips.push_back({lamp::Surface::Shade, 26, lamp::ByteOrder::GRBW,  9});
  hw.strips.push_back({lamp::Surface::Base,  12, lamp::ByteOrder::GRBW, 24});
  TEST_ASSERT_TRUE(lamp::validateHwConfig(hw));
}

void test_sigma_shade_over_255_invalid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 14, lamp::ByteOrder::GRBW, 200});
  hw.strips.push_back({lamp::Surface::Shade, 27, lamp::ByteOrder::GRBW, 200});
  hw.strips.push_back({lamp::Surface::Base,  12, lamp::ByteOrder::GRBW,  24});
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_sigma_base_over_255_invalid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 14, lamp::ByteOrder::GRBW, 38});
  hw.strips.push_back({lamp::Surface::Base,  12, lamp::ByteOrder::GRBW, 200});
  hw.strips.push_back({lamp::Surface::Base,  27, lamp::ByteOrder::GRBW, 200});
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_multi_broadcast_invalid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW, 0, "Shade", 1});
  hw.strips.push_back({lamp::Surface::Base,  14, lamp::ByteOrder::GRBW, 0, "Base",  1});
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_single_broadcast_valid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW, 0, "Shade", 1});
  hw.strips.push_back({lamp::Surface::Base,  14, lamp::ByteOrder::GRBW, 0, "Base",  0});
  TEST_ASSERT_TRUE(lamp::validateHwConfig(hw));
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_empty_strips_invalid);
  RUN_TEST(test_duplicate_pins_invalid);
  RUN_TEST(test_well_formed_config_valid);
  RUN_TEST(test_missing_shade_invalid);
  RUN_TEST(test_missing_base_invalid);
  RUN_TEST(test_multi_strip_same_role_valid);
  RUN_TEST(test_sigma_shade_over_255_invalid);
  RUN_TEST(test_sigma_base_over_255_invalid);
  RUN_TEST(test_multi_broadcast_invalid);
  RUN_TEST(test_single_broadcast_valid);
  return UNITY_END();
}
