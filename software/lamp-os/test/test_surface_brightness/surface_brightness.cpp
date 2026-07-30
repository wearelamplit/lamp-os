// Native-host tests for the per-surface brightness trim math that
// setAllStripsBrightness fans out. The production strip write drags in
// FrameBuffer / NeoPixel / PowerGovernor and is not natively linkable, so
// this compiles the real scale8 + calculateBrightnessLevel and exercises the
// exact per-surface expression the funnel uses:
//   applied      = min(scaledLevel, ceiling)
//   surfaceValue = scale8(applied, factor)

#include <unity.h>

#include <algorithm>
#include <cstdint>

#include "../../src/util/color.cpp"
#include "../../src/util/levels.cpp"

using lamp::calculateBrightnessLevel;
using lamp::scale8;

void setUp(void) {}
void tearDown(void) {}

// Mirrors the per-surface expression inside setAllStripsBrightness.
static uint8_t surfaceApplied(uint8_t scaledLevel, uint8_t ceiling,
                              uint8_t factor) {
  return scale8(std::min(scaledLevel, ceiling), factor);
}

// factor 255 on both surfaces is a no-op: identical to the pre-trim
// min(scaledLevel, ceiling) for every level/ceiling pair.
static void test_factor_255_is_pre_change_identity() {
  for (uint16_t ceiling = 0; ceiling <= 255; ceiling += 5) {
    for (uint16_t level = 0; level <= 255; level += 5) {
      const uint8_t expected =
          std::min<uint8_t>(static_cast<uint8_t>(level),
                            static_cast<uint8_t>(ceiling));
      TEST_ASSERT_EQUAL_UINT8(
          expected, surfaceApplied(static_cast<uint8_t>(level),
                                   static_cast<uint8_t>(ceiling), 255));
    }
  }
}

// A shade trim scales only the shade surface; base stays at the governed
// level (and vice versa).
static void test_trim_is_per_surface_independent() {
  const uint8_t level = 200, ceiling = 255;
  const uint8_t shadeFactor = 128, baseFactor = 255;

  const uint8_t shade = surfaceApplied(level, ceiling, shadeFactor);
  const uint8_t base = surfaceApplied(level, ceiling, baseFactor);

  TEST_ASSERT_EQUAL_UINT8(200, base);  // untrimmed surface untouched
  TEST_ASSERT_TRUE(shade < base);      // trimmed surface scaled down
  TEST_ASSERT_EQUAL_UINT8(scale8(200, 128), shade);
}

// The trim only scales DOWN: no factor (including 255) ever lifts a surface
// above the governor ceiling.
static void test_never_exceeds_ceiling() {
  const uint8_t ceiling = 90;
  for (uint16_t level = 0; level <= 255; level += 3) {
    for (uint16_t factor = 0; factor <= 255; factor += 3) {
      const uint8_t v = surfaceApplied(static_cast<uint8_t>(level), ceiling,
                                       static_cast<uint8_t>(factor));
      TEST_ASSERT_TRUE(v <= ceiling);
    }
  }
}

// setSurfaceBrightness maps a 0-100 percent to a 0-255 factor via
// calculateBrightnessLevel(255, pct): 100 -> 255 (identity), 0 -> 0.
static void test_percent_to_factor_mapping() {
  TEST_ASSERT_EQUAL_UINT8(0, calculateBrightnessLevel(255, 0));
  TEST_ASSERT_EQUAL_UINT8(127, calculateBrightnessLevel(255, 50));
  TEST_ASSERT_EQUAL_UINT8(255, calculateBrightnessLevel(255, 100));
  // A 100% factor round-trips a value unchanged through scale8.
  TEST_ASSERT_EQUAL_UINT8(180, scale8(180, calculateBrightnessLevel(255, 100)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_factor_255_is_pre_change_identity);
  RUN_TEST(test_trim_is_per_surface_independent);
  RUN_TEST(test_never_exceeds_ceiling);
  RUN_TEST(test_percent_to_factor_mapping);
  return UNITY_END();
}
