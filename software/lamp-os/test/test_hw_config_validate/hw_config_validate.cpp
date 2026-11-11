// software/lamp-os/test/test_hw_config_validate/hw_config_validate.cpp
//
// Verifies validateHwConfig() detects malformed configs. Native test —
// validates the predicate only, not the FATAL halt path (which needs
// Arduino).

#include <unity.h>

#include <string>
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

void test_zero_supply_budget_invalid() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW});
  hw.strips.push_back({lamp::Surface::Base,  14, lamp::ByteOrder::GRBW});
  hw.supplyBudgetMa = 0;
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

static lamp::HwConfig twoStrip() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW});
  hw.strips.push_back({lamp::Surface::Base,  14, lamp::ByteOrder::GRBW});
  return hw;
}

void test_inputs_valid_distinct_pins() {
  lamp::HwConfig hw = twoStrip();
  hw.inputs.push_back({1, lamp::InputType::Button, 19});
  hw.inputs.push_back({2, lamp::InputType::Touch,  4});
  TEST_ASSERT_TRUE(lamp::validateHwConfig(hw));
}

void test_input_colliding_with_strip_pin_invalid() {
  lamp::HwConfig hw = twoStrip();  // strips on 12, 14
  hw.inputs.push_back({1, lamp::InputType::Touch, 12});
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

void test_input_colliding_with_input_pin_invalid() {
  lamp::HwConfig hw = twoStrip();
  hw.inputs.push_back({1, lamp::InputType::Button, 19});
  hw.inputs.push_back({2, lamp::InputType::Touch,  19});
  TEST_ASSERT_FALSE(lamp::validateHwConfig(hw));
}

static lamp::HwConfig standardLike() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW});
  hw.strips.push_back({lamp::Surface::Base, 14, lamp::ByteOrder::GRBW});
  return hw;
}

void test_strip_data_pin_predicate() {
  for (uint8_t ok : {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33}) {
    TEST_ASSERT_TRUE_MESSAGE(lamp::isStripDataPin(ok), std::to_string(ok).c_str());
  }
  for (uint8_t bad : {0, 1, 3, 6, 7, 8, 9, 10, 11, 20, 24, 28, 31, 34, 36, 39, 40, 255}) {
    TEST_ASSERT_FALSE_MESSAGE(lamp::isStripDataPin(bad), std::to_string(bad).c_str());
  }
}

void test_no_request_keeps_variant_pins() {
  const auto pins = lamp::resolveStripPins(standardLike(), {0, 0});
  TEST_ASSERT_EQUAL_UINT8(12, pins[0]);
  TEST_ASSERT_EQUAL_UINT8(14, pins[1]);
  const auto empty = lamp::resolveStripPins(standardLike(), {});
  TEST_ASSERT_EQUAL_UINT8(14, empty[1]);
}

void test_requested_pin_replaces_variant_pin() {
  const auto pins = lamp::resolveStripPins(standardLike(), {0, 27});
  TEST_ASSERT_EQUAL_UINT8(12, pins[0]);
  TEST_ASSERT_EQUAL_UINT8(27, pins[1]);
}

void test_unusable_request_keeps_variant_pin() {
  const auto pins = lamp::resolveStripPins(standardLike(), {0, 34});
  TEST_ASSERT_EQUAL_UINT8(14, pins[1]);
}

void test_request_onto_another_strips_pin_rejected() {
  const auto pins = lamp::resolveStripPins(standardLike(), {0, 12});
  TEST_ASSERT_EQUAL_UINT8(12, pins[0]);
  TEST_ASSERT_EQUAL_UINT8(14, pins[1]);
}

void test_two_requests_for_same_pin_both_rejected() {
  const auto pins = lamp::resolveStripPins(standardLike(), {27, 27});
  TEST_ASSERT_EQUAL_UINT8(12, pins[0]);
  TEST_ASSERT_EQUAL_UINT8(14, pins[1]);
}

void test_swapped_pins_allowed() {
  const auto pins = lamp::resolveStripPins(standardLike(), {14, 12});
  TEST_ASSERT_EQUAL_UINT8(14, pins[0]);
  TEST_ASSERT_EQUAL_UINT8(12, pins[1]);
}

void test_revert_cascades_to_dependent_request() {
  // Shade's 34 is unusable so it stays on 12, which the base asked for.
  const auto pins = lamp::resolveStripPins(standardLike(), {34, 12});
  TEST_ASSERT_EQUAL_UINT8(12, pins[0]);
  TEST_ASSERT_EQUAL_UINT8(14, pins[1]);
}

// The shade's and base's clash reverts them only after the third strip's
// request for the shade's pin was checked, so it takes a second pass.
void test_revert_reaches_a_request_checked_before_it() {
  lamp::HwConfig hw = standardLike();
  hw.strips.push_back({lamp::Surface::Base, 26, lamp::ByteOrder::GRBW});
  const auto pins = lamp::resolveStripPins(hw, {27, 27, 12});
  TEST_ASSERT_EQUAL_UINT8(12, pins[0]);
  TEST_ASSERT_EQUAL_UINT8(14, pins[1]);
  TEST_ASSERT_EQUAL_UINT8(26, pins[2]);
}

void test_request_onto_input_pin_rejected() {
  lamp::HwConfig hw = standardLike();
  hw.inputs.push_back({1, lamp::InputType::Button, 27});
  const auto pins = lamp::resolveStripPins(hw, {0, 27});
  TEST_ASSERT_EQUAL_UINT8(14, pins[1]);
}

struct PinSeg {
  uint8_t pin = 0;
};

// Roles interleaved and the base declared first, as loaf and snafu do.
static lamp::HwConfig interleaved() {
  lamp::HwConfig hw{};
  hw.strips.push_back({lamp::Surface::Base, 14, lamp::ByteOrder::GRBW});
  hw.strips.push_back({lamp::Surface::Shade, 12, lamp::ByteOrder::GRBW});
  hw.strips.push_back({lamp::Surface::Base, 26, lamp::ByteOrder::GRBW});
  hw.strips.push_back({lamp::Surface::Shade, 27, lamp::ByteOrder::GRBW});
  return hw;
}

void test_segment_pins_pair_with_their_roles_strips_in_order() {
  std::vector<PinSeg> shade = {{13}, {0}};
  std::vector<PinSeg> base = {{0}, {32}};
  const auto pins = lamp::applySegmentPins(interleaved(), shade, base);
  TEST_ASSERT_EQUAL_UINT8(14, pins[0]);
  TEST_ASSERT_EQUAL_UINT8(13, pins[1]);
  TEST_ASSERT_EQUAL_UINT8(32, pins[2]);
  TEST_ASSERT_EQUAL_UINT8(27, pins[3]);
  TEST_ASSERT_EQUAL_UINT8(13, shade[0].pin);
  TEST_ASSERT_EQUAL_UINT8(32, base[1].pin);
}

void test_segment_pin_that_does_not_take_effect_is_cleared() {
  std::vector<PinSeg> shade = {{26}, {33}};
  std::vector<PinSeg> base = {{0}, {0}};
  const auto pins = lamp::applySegmentPins(interleaved(), shade, base);
  TEST_ASSERT_EQUAL_UINT8(12, pins[1]);
  TEST_ASSERT_EQUAL_UINT8(33, pins[3]);
  TEST_ASSERT_EQUAL_UINT8(0, shade[0].pin);
  TEST_ASSERT_EQUAL_UINT8(33, shade[1].pin);
}

void test_strip_without_a_segment_keeps_its_variant_pin() {
  std::vector<PinSeg> shade = {{13}};
  std::vector<PinSeg> base = {};
  const auto pins = lamp::applySegmentPins(interleaved(), shade, base);
  TEST_ASSERT_EQUAL_UINT8(14, pins[0]);
  TEST_ASSERT_EQUAL_UINT8(13, pins[1]);
  TEST_ASSERT_EQUAL_UINT8(26, pins[2]);
  TEST_ASSERT_EQUAL_UINT8(27, pins[3]);
}

void test_segment_pin_without_a_strip_is_cleared() {
  std::vector<PinSeg> shade = {{0}, {0}, {13}};
  std::vector<PinSeg> base = {{0}, {0}, {32}};
  lamp::applySegmentPins(interleaved(), shade, base);
  TEST_ASSERT_EQUAL_UINT8(0, shade[2].pin);
  TEST_ASSERT_EQUAL_UINT8(0, base[2].pin);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_strip_data_pin_predicate);
  RUN_TEST(test_no_request_keeps_variant_pins);
  RUN_TEST(test_requested_pin_replaces_variant_pin);
  RUN_TEST(test_unusable_request_keeps_variant_pin);
  RUN_TEST(test_request_onto_another_strips_pin_rejected);
  RUN_TEST(test_two_requests_for_same_pin_both_rejected);
  RUN_TEST(test_swapped_pins_allowed);
  RUN_TEST(test_revert_cascades_to_dependent_request);
  RUN_TEST(test_revert_reaches_a_request_checked_before_it);
  RUN_TEST(test_request_onto_input_pin_rejected);
  RUN_TEST(test_segment_pins_pair_with_their_roles_strips_in_order);
  RUN_TEST(test_segment_pin_that_does_not_take_effect_is_cleared);
  RUN_TEST(test_strip_without_a_segment_keeps_its_variant_pin);
  RUN_TEST(test_segment_pin_without_a_strip_is_cleared);
  RUN_TEST(test_zero_supply_budget_invalid);
  RUN_TEST(test_inputs_valid_distinct_pins);
  RUN_TEST(test_input_colliding_with_strip_pin_invalid);
  RUN_TEST(test_input_colliding_with_input_pin_invalid);
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
