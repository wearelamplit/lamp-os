// Golden vectors shared with the app's wisp_palette_hash_test.dart: the wisp
// stamps manualPaletteId() as its palette id and the app confirms a write by
// matching the same hash on the status NOTIFY, so both sides must agree on the
// canonical wire bytes (r,g,b per color, plus w only when w != 0). The strip
// vector exercises the app's trailing-W cap strip: the wisp stores what the
// app sent, so its trailing colours arrive with w == 0.

#include <unity.h>

#include <string>
#include <vector>

#include "config/manual_palette_color.hpp"
#include "paint/palette_id.hpp"

void setUp() {}
void tearDown() {}

void test_w_zero_palette(void) {
  std::vector<wisp::ManualPaletteColor> cols = {{1, 2, 3, 0}, {4, 5, 6, 0}};
  TEST_ASSERT_EQUAL_STRING("03d252aa", wisp::manualPaletteId(cols).c_str());
}

void test_w_present_palette(void) {
  std::vector<wisp::ManualPaletteColor> cols = {{10, 20, 30, 0},
                                                {40, 50, 60, 70}};
  TEST_ASSERT_EQUAL_STRING("59a8ae37", wisp::manualPaletteId(cols).c_str());
}

void test_ten_w_colours_arrive_whole(void) {
  std::vector<wisp::ManualPaletteColor> cols(10, {255, 255, 255, 255});
  TEST_ASSERT_EQUAL_STRING("999fdcbd", wisp::manualPaletteId(cols).c_str());
}

// The app's 30 x RGBW-255 strip vector as stored: the app's 512 B op cap
// strips W from the last 20.
void test_strip_palette_trailing_colours_arrive_without_w(void) {
  std::vector<wisp::ManualPaletteColor> cols(10, {255, 255, 255, 255});
  cols.insert(cols.end(), 20, {255, 255, 255, 0});
  TEST_ASSERT_EQUAL_STRING("2eba5ad1", wisp::manualPaletteId(cols).c_str());
}

void test_empty_palette_is_fnv_offset_basis(void) {
  std::vector<wisp::ManualPaletteColor> cols;
  TEST_ASSERT_EQUAL_STRING("811c9dc5", wisp::manualPaletteId(cols).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_w_zero_palette);
  RUN_TEST(test_w_present_palette);
  RUN_TEST(test_ten_w_colours_arrive_whole);
  RUN_TEST(test_strip_palette_trailing_colours_arrive_without_w);
  RUN_TEST(test_empty_palette_is_fnv_offset_basis);
  return UNITY_END();
}
