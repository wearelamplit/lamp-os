// Pins glitchBlockPlan's scatter->grain-block math, the core of glitchy's
// density. Arduino-free so it runs native.
#include <unity.h>

#include "expressions/primitives.hpp"

using lamp::glitchBlockPlan;
using lamp::GlitchPlan;

void setUp() {}
void tearDown() {}

void test_scatter_zero_is_solid_sentinel() {
  GlitchPlan p = glitchBlockPlan(0, 36);
  TEST_ASSERT_EQUAL_UINT16(0, p.grainPx);
  TEST_ASSERT_EQUAL_UINT16(0, p.blocksWanted);
}

void test_scatter_five_is_grain_one() {
  GlitchPlan p = glitchBlockPlan(5, 36);
  TEST_ASSERT_EQUAL_UINT16(1, p.grainPx);
}

void test_empty_region_wants_nothing() {
  GlitchPlan p = glitchBlockPlan(5, 0);
  TEST_ASSERT_EQUAL_UINT16(0, p.blocksWanted);
}

void test_grain_larger_than_region_wants_nothing() {
  GlitchPlan p = glitchBlockPlan(1, 4);  // grain 6 > region 4
  TEST_ASSERT_EQUAL_UINT16(0, p.slotCount);
  TEST_ASSERT_EQUAL_UINT16(0, p.blocksWanted);
}

void test_out_of_range_clamps_to_five() {
  GlitchPlan hi = glitchBlockPlan(99, 36);
  GlitchPlan five = glitchBlockPlan(5, 36);
  TEST_ASSERT_EQUAL_UINT16(five.grainPx, hi.grainPx);
  TEST_ASSERT_EQUAL_UINT16(five.blocksWanted, hi.blocksWanted);
}

// Density is exact: blocksWanted is the rounded fraction of slots and never
// exceeds slotCount, so it reflects DISTINCT slots, not a 1-1/e collision cap.
void test_density_is_real_per_level() {
  const uint16_t region = 36;
  struct Expect { uint16_t level, grain, slots, blocks; };
  const Expect table[] = {
    {1, 6,  6,  5},
    {2, 4,  9,  6},
    {3, 3, 12,  6},
    {4, 2, 18,  7},
    {5, 1, 36, 11},
  };
  for (const Expect& e : table) {
    GlitchPlan p = glitchBlockPlan(e.level, region);
    TEST_ASSERT_EQUAL_UINT16(e.grain, p.grainPx);
    TEST_ASSERT_EQUAL_UINT16(e.slots, p.slotCount);
    TEST_ASSERT_EQUAL_UINT16(e.blocks, p.blocksWanted);
    TEST_ASSERT_TRUE(p.blocksWanted <= p.slotCount);
  }
}

// Higher scatter reads sparser: realized lit pixels (blocks * grain) strictly
// decrease from level 1 to 5.
void test_monotonic_sparsity() {
  const uint16_t region = 36;
  uint16_t prev = 0xFFFF;
  for (uint16_t level = 1; level <= 5; ++level) {
    GlitchPlan p = glitchBlockPlan(level, region);
    const uint16_t lit = p.blocksWanted * p.grainPx;
    TEST_ASSERT_TRUE(lit < prev);
    prev = lit;
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_scatter_zero_is_solid_sentinel);
  RUN_TEST(test_scatter_five_is_grain_one);
  RUN_TEST(test_empty_region_wants_nothing);
  RUN_TEST(test_grain_larger_than_region_wants_nothing);
  RUN_TEST(test_out_of_range_clamps_to_five);
  RUN_TEST(test_density_is_real_per_level);
  RUN_TEST(test_monotonic_sparsity);
  return UNITY_END();
}
