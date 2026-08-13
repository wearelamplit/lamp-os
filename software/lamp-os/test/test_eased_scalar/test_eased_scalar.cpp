#include <unity.h>

#include "util/eased_scalar.hpp"

using lamp::EasedScalar;
using lamp::opacityTarget;
using lamp::kDefaultEaseRate;

void setUp() {}
void tearDown() {}

void test_eases_up_reaches_and_holds() {
  EasedScalar s(0.1f);
  s.setTarget(1.0f);
  for (int i = 0; i < 10; ++i) s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, s.value());
  // Extra ticks never overshoot.
  s.tick();
  s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, s.value());
}

void test_eases_down_symmetric() {
  EasedScalar s(0.1f, 1.0f);
  s.setTarget(0.0f);
  for (int i = 0; i < 10; ++i) s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.value());
  s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.value());
}

void test_reverses_mid_flight_no_jump() {
  const float r = 0.1f;
  EasedScalar s(r);
  s.setTarget(1.0f);
  for (int i = 0; i < 5; ++i) s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5 * r, s.value());  // 0.5
  s.setTarget(0.0f);
  s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 4 * r, s.value());  // turned around from 0.5, no jump
  s.tick();
  s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2 * r, s.value());
}

void test_clamps_target_and_value() {
  EasedScalar s(0.1f);
  s.setTarget(5.0f);
  for (int i = 0; i < 20; ++i) s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, s.value());  // never above 1
  s.setTarget(-3.0f);
  for (int i = 0; i < 20; ++i) s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.value());  // never below 0
}

void test_rate_honored() {
  const float r = 0.05f;
  EasedScalar s(r);
  s.setTarget(1.0f);
  for (int n = 1; n <= 5; ++n) {
    s.tick();
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, n * r, s.value());
  }
}

void test_default_rate_matches_30_frames() {
  EasedScalar s;  // default rate
  s.setTarget(1.0f);
  for (int i = 0; i < 30; ++i) s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, s.value());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f / 30.0f, kDefaultEaseRate);
}

void test_lands_exactly_on_non_multiple_target() {
  // Core promise: land on the EXACT target, not the nearest rate multiple.
  EasedScalar s(0.1f);
  s.setTarget(0.55f);
  for (int i = 0; i < 20; ++i) s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.55f, s.value());
  s.tick();
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.55f, s.value());
}

void test_opacity_target_clamps_out_of_range() {
  // Out-of-range w can't yield a negative or over-unity opacity.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, opacityTarget(true, 0.8f, 0.0f, 2.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, opacityTarget(true, 1.0f, 0.0f, -1.0f));
}

void test_opacity_target_composition() {
  // enabled=false -> 0 regardless of everything.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, opacityTarget(false, 0.8f, 0.3f, 1.0f));
  // wispDimFloor=1 -> userOpacity regardless of w.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.8f, opacityTarget(true, 0.8f, 1.0f, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.8f, opacityTarget(true, 0.8f, 1.0f, 1.0f));
  // wispDimFloor=0, w=1 -> 0.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, opacityTarget(true, 0.8f, 0.0f, 1.0f));
  // wispDimFloor=0.3, w=1 -> 0.3 * userOpacity.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.3f * 0.8f, opacityTarget(true, 0.8f, 0.3f, 1.0f));
  // w=0 ignores the floor entirely.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.8f, opacityTarget(true, 0.8f, 0.3f, 0.0f));
}

// Expression::wispDimScale() is opacityTarget(true, 1.0, wispDimFloor, w).
// Pin that mapping: a yielding floor dims to the floor at full presence and
// returns to full as presence eases to 0; a floor of 1.0 ignores the wisp.
void test_wisp_dim_scale_contract() {
  const float yield = 0.3f;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, yield, opacityTarget(true, 1.0f, yield, 1.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.65f, opacityTarget(true, 1.0f, yield, 0.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, opacityTarget(true, 1.0f, yield, 0.0f));
  // Non-yielding: full at every presence.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, opacityTarget(true, 1.0f, 1.0f, 1.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, opacityTarget(true, 1.0f, 1.0f, 0.0f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_eases_up_reaches_and_holds);
  RUN_TEST(test_eases_down_symmetric);
  RUN_TEST(test_reverses_mid_flight_no_jump);
  RUN_TEST(test_clamps_target_and_value);
  RUN_TEST(test_rate_honored);
  RUN_TEST(test_default_rate_matches_30_frames);
  RUN_TEST(test_lands_exactly_on_non_multiple_target);
  RUN_TEST(test_opacity_target_clamps_out_of_range);
  RUN_TEST(test_opacity_target_composition);
  RUN_TEST(test_wisp_dim_scale_contract);
  return UNITY_END();
}
