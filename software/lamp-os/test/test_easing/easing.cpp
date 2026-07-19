#include <unity.h>

#include <cmath>

#include "util/easing.hpp"

using lamp::Easing;
using lamp::applyEasing;
using lamp::easeStep;
using lamp::kFloatDwell;

// Triangle 0->1->0 over phase, matching BreathingExpression::draw().
static float breathTriangle(float phase) {
  return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
}

static const Easing kAll[] = {Easing::Linear, Easing::Smooth, Easing::Float,
                              Easing::Settle, Easing::Swell};

void setUp() {}
void tearDown() {}

void test_endpoints_map_to_endpoints() {
  for (Easing e : kAll) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, applyEasing(e, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, applyEasing(e, 1.0f));
  }
}

void test_monotonic_non_decreasing() {
  for (Easing e : kAll) {
    float prev = applyEasing(e, 0.0f);
    for (int i = 1; i <= 100; ++i) {
      const float y = applyEasing(e, i / 100.0f);
      TEST_ASSERT_TRUE(y >= prev - 1e-6f);
      prev = y;
    }
  }
}

void test_linear_is_identity() {
  for (int i = 0; i <= 100; ++i) {
    const float t = i / 100.0f;
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, t, applyEasing(Easing::Linear, t));
  }
}

void test_smooth_and_float_pass_through_half() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, applyEasing(Easing::Smooth, 0.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, applyEasing(Easing::Float, 0.5f));
}

void test_settle_front_loaded_swell_back_loaded() {
  TEST_ASSERT_TRUE(applyEasing(Easing::Settle, 0.5f) > 0.5f);
  TEST_ASSERT_TRUE(applyEasing(Easing::Swell, 0.5f) < 0.5f);
}

void test_float_dwells_at_ends() {
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, applyEasing(Easing::Float, kFloatDwell * 0.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, applyEasing(Easing::Float, 1.0f - kFloatDwell * 0.5f));
}

void test_clamps_out_of_range() {
  for (Easing e : kAll) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, applyEasing(e, -0.5f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, applyEasing(e, 1.5f));
  }
}

// Shifty's fade step: Linear default leaves the step untouched (bit-identical
// linear fade); a non-linear curve reshapes it.
void test_easestep_linear_identity() {
  const uint32_t dur = 60000;
  for (uint32_t s = 0; s <= dur; s += 5000) {
    TEST_ASSERT_EQUAL_UINT32(s, easeStep(s, dur, Easing::Linear));
  }
  TEST_ASSERT_EQUAL_UINT32(1234, easeStep(1234, 0, Easing::Swell));  // dur==0 guard
}

void test_easestep_nonlinear_reshapes() {
  const uint32_t dur = 60000;
  TEST_ASSERT_EQUAL_UINT32(0, easeStep(0, dur, Easing::Swell));
  TEST_ASSERT_EQUAL_UINT32(dur, easeStep(dur, dur, Easing::Swell));
  // Swell(0.5) = 0.25, so the midpoint step is pulled back to a quarter.
  TEST_ASSERT_UINT32_WITHIN(50, dur / 4, easeStep(dur / 2, dur, Easing::Swell));
  TEST_ASSERT_TRUE(easeStep(dur / 2, dur, Easing::Swell) < dur / 2);
}

// Breathing's default (Smooth) reproduces the old raised-cosine breath within
// ~2%; Linear (a bare triangle) departs from it materially.
void test_breath_smooth_matches_raised_cosine() {
  const float pi = 3.14159265358979323846f;
  float maxSmoothErr = 0.0f, maxLinearErr = 0.0f;
  for (int i = 0; i <= 100; ++i) {
    const float phase = i / 100.0f;
    const float ref = 0.5f - 0.5f * std::cos(phase * 2.0f * pi);
    const float tri = breathTriangle(phase);
    maxSmoothErr = std::fmax(maxSmoothErr, std::fabs(applyEasing(Easing::Smooth, tri) - ref));
    maxLinearErr = std::fmax(maxLinearErr, std::fabs(applyEasing(Easing::Linear, tri) - ref));
  }
  TEST_ASSERT_TRUE(maxSmoothErr < 0.02f);
  TEST_ASSERT_TRUE(maxLinearErr > 0.09f);
}

// social.cpp's greeting easedRamp() maps a ramp position to a 0..255 blend
// amount via applyEasing(curve, t) * 255. Swell (warm arrival) lags linear;
// Smooth (neutral/snub) hits the halfway blend at the halfway frame.
static uint32_t rampPos(Easing curve, float t) {
  return static_cast<uint32_t>(applyEasing(curve, t) * 255.0f + 0.5f);
}

void test_greeting_ramp_position_follows_curve() {
  TEST_ASSERT_EQUAL_UINT32(0, rampPos(Easing::Swell, 0.0f));
  TEST_ASSERT_EQUAL_UINT32(255, rampPos(Easing::Swell, 1.0f));
  TEST_ASSERT_UINT32_WITHIN(2, 64, rampPos(Easing::Swell, 0.5f));   // 0.25 * 255
  TEST_ASSERT_TRUE(rampPos(Easing::Swell, 0.5f) < 128);
  TEST_ASSERT_UINT32_WITHIN(2, 128, rampPos(Easing::Smooth, 0.5f));
}

// Scalar model of social.cpp's easedRamp(): the ramp POSITION is eased once
// (rampPos above), then the blend from start to end is linear on that
// position, so the curve is applied exactly once. Endpoints must be exact
// (step 0 → start, step span → end) regardless of curve.
static uint8_t easedRampByte(uint8_t start, uint8_t end, Easing curve,
                             uint32_t step, uint32_t span) {
  const float t = span > 0 ? static_cast<float>(step) / static_cast<float>(span) : 1.0f;
  const uint32_t pos = static_cast<uint32_t>(applyEasing(curve, t) * 255.0f + 0.5f);
  const int delta = static_cast<int>(end) - static_cast<int>(start);
  return static_cast<uint8_t>(start + (delta * static_cast<int>(pos)) / 255);
}

void test_greeting_eased_ramp_contract() {
  // Endpoints exact for every curve.
  const Easing curves[] = {Easing::Smooth, Easing::Swell, Easing::Float};
  for (Easing e : curves) {
    TEST_ASSERT_EQUAL_UINT8(0,   easedRampByte(0, 200, e, 0,   300));
    TEST_ASSERT_EQUAL_UINT8(200, easedRampByte(0, 200, e, 300, 300));
  }
  // span == 0 collapses to end (the draw-side guard).
  TEST_ASSERT_EQUAL_UINT8(200, easedRampByte(0, 200, Easing::Float, 0, 0));
  // Swell (warm arrival) lags Smooth at the midpoint.
  TEST_ASSERT_TRUE(easedRampByte(0, 200, Easing::Swell,  150, 300) <
                   easedRampByte(0, 200, Easing::Smooth, 150, 300));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_endpoints_map_to_endpoints);
  RUN_TEST(test_monotonic_non_decreasing);
  RUN_TEST(test_linear_is_identity);
  RUN_TEST(test_smooth_and_float_pass_through_half);
  RUN_TEST(test_settle_front_loaded_swell_back_loaded);
  RUN_TEST(test_float_dwells_at_ends);
  RUN_TEST(test_clamps_out_of_range);
  RUN_TEST(test_easestep_linear_identity);
  RUN_TEST(test_easestep_nonlinear_reshapes);
  RUN_TEST(test_breath_smooth_matches_raised_cosine);
  RUN_TEST(test_greeting_ramp_position_follows_curve);
  RUN_TEST(test_greeting_eased_ramp_contract);
  return UNITY_END();
}
