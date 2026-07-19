// Native-host tests for lamp::applyDimFactor — the shared floored-dim helper
// used by crowd-dim and space-dim. Compiles the real implementation into the
// TU (same pattern as test_config_codec).

#include <unity.h>

#include <cstdint>

#include "../../src/util/color.cpp"
#include "../../src/util/levels.cpp"

using lamp::applyDimFactor;

void setUp(void) {}
void tearDown(void) {}

// Deliberate off (baseline 0) is preserved — the bug fix. A crowd/space dim
// must never light a lamp the user turned off.
static void test_baseline_zero_stays_off() {
  TEST_ASSERT_EQUAL_UINT8(0, applyDimFactor(0, 0.05f, 30));
  TEST_ASSERT_EQUAL_UINT8(0, applyDimFactor(0, 0.5f, 30));
  TEST_ASSERT_EQUAL_UINT8(0, applyDimFactor(0, 1.0f, 30));
  TEST_ASSERT_EQUAL_UINT8(0, applyDimFactor(0, 0.0f, 20));
}

// Heavy dim floors the OUTPUT at floorPct.
static void test_heavy_dim_floored() {
  TEST_ASSERT_EQUAL_UINT8(30, applyDimFactor(100, 0.05f, 30));
}

// A baseline already below the floor is left alone, not brightened up to it.
static void test_below_floor_not_brightened() {
  TEST_ASSERT_EQUAL_UINT8(25, applyDimFactor(25, 0.05f, 30));
}

// factor 1.0 returns baseline for all baselines; the no-brighten guard also
// covers a sub-floor baseline (subsumes the old factor>=1 early-out).
static void test_factor_one_is_identity() {
  TEST_ASSERT_EQUAL_UINT8(100, applyDimFactor(100, 1.0f, 30));
  TEST_ASSERT_EQUAL_UINT8(15, applyDimFactor(15, 1.0f, 30));
  for (uint16_t raw = 0; raw <= 255; ++raw) {
    TEST_ASSERT_EQUAL_UINT8(raw, applyDimFactor(static_cast<uint8_t>(raw), 1.0f, 30));
  }
}

// Space-dim floor path: above the floor, plain scale.
static void test_space_dim_floor() {
  TEST_ASSERT_EQUAL_UINT8(50, applyDimFactor(100, 0.5f, 20));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_baseline_zero_stays_off);
  RUN_TEST(test_heavy_dim_floored);
  RUN_TEST(test_below_floor_not_brightened);
  RUN_TEST(test_factor_one_is_identity);
  RUN_TEST(test_space_dim_floor);
  return UNITY_END();
}
