// The distributor reports a palette style change only when the style moves,
// so a resent setSwatches re-assigns no lamp and cuts no drift fade.

#include <unity.h>

#include "paint/paint_distributor.hpp"

void setUp() {}
void tearDown() {}

void test_a_resent_style_reports_no_change() {
  wisp::PaintDistributor paint;
  TEST_ASSERT_FALSE(paint.setPaletteStyle(wisp::PaletteStyle::Gradient));
  TEST_ASSERT_TRUE(paint.setPaletteStyle(wisp::PaletteStyle::Swatches));
  TEST_ASSERT_FALSE(paint.setPaletteStyle(wisp::PaletteStyle::Swatches));
  TEST_ASSERT_TRUE(paint.setPaletteStyle(wisp::PaletteStyle::Gradient));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_resent_style_reports_no_change);
  return UNITY_END();
}
