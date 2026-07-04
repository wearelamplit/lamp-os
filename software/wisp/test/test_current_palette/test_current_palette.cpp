#include <unity.h>

// Native-test seam: current_palette.cpp is excluded from the native src filter
// and carries no-op FreeRTOS stubs, so pulling it in directly exercises
// update() on the host.
#include "paint/current_palette.cpp"

void setUp() {}
void tearDown() {}

static Palette makeFloat(const PaletteColor& c) {
  Palette p;
  p.id = "warm";
  p.colors.push_back(c);
  return p;
}

// W is warm white; an amber-encoded palette folds its warmth into W. Without
// the fold W stays 0 and the warm palette renders cold.
void test_amber_folds_into_warm_white() {
  PaletteColor c;
  c.r = 0.3f; c.g = 0.0f; c.b = 0.1f; c.w = 0.0f; c.am = 0.5f; c.u = 0.0f;
  wisp::CurrentPalette cp;
  cp.update(makeFloat(c), 0);
  TEST_ASSERT_EQUAL(1, cp.colors().size());
  const wisp::RGBW out = cp.colors()[0];
  TEST_ASSERT_TRUE(out.w > 0);          // amber lifts the warm-white channel
}

// hexColors (pure 24-bit RGB) has no amber and must pass through unchanged.
void test_hex_colors_unaffected() {
  Palette p;
  p.id = "hex";
  p.hexColors.push_back(0x00FFD800);  // amber-ish RGB, no separate channels
  wisp::CurrentPalette cp;
  cp.update(p, 0);
  const wisp::RGBW out = cp.colors()[0];
  TEST_ASSERT_EQUAL_UINT8(0xFF, out.r);
  TEST_ASSERT_EQUAL_UINT8(0xD8, out.g);
  TEST_ASSERT_EQUAL_UINT8(0x00, out.b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_amber_folds_into_warm_white);
  RUN_TEST(test_hex_colors_unaffected);
  return UNITY_END();
}
