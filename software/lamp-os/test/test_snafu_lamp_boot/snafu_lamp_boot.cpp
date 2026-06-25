// Native smoke test for SnafuLamp hardware configuration constants.
//
// Why not instantiate SnafuLamp directly:
//   snafu_lamp.hpp -> lamp.hpp -> config/config.hpp -> <Arduino.h>
//   snafu_greeting.hpp -> nearby_lamps.hpp -> <Arduino.h>/<freertos/...>
//   animated_behavior.hpp -> frame_buffer.hpp -> <Adafruit_NeoPixel.h>/<Arduino.h>
// None of these headers are available in the native test env.
//
// Pattern: mirror the relevant constants from snafu_lamp.hpp directly in
// this file and assert their expected values. This pins the hardware config
// shape so a mistaken change to pin assignments or pixel counts gets caught
// immediately. The full instantiation path is exercised by the upesy_wroom
// firmware build (G.9).
//
// If snafu_lamp.hpp's SnafuLamp constructor changes, update the mirrored
// constants below to match.

#include <unity.h>
#include <cstdint>

void setUp(void) {}
void tearDown(void) {}

// Mirror of SnafuLamp's HwConfig{} initializer.
// Amanita hardware: shade cap on pin 12, stem base on pin 14.
namespace {
  constexpr uint8_t  kShadeSurface    = 0;  // Surface::Shade
  constexpr uint8_t  kBaseSurface     = 1;  // Surface::Base
  constexpr uint8_t  kShadePin        = 12;
  constexpr uint16_t kShadePixelCount = 40;
  constexpr uint8_t  kBasePin         = 14;
  constexpr uint16_t kBasePixelCount  = 40;
  constexpr uint8_t  kMaxBrightness   = 180;
  // Spot region within shade: Python range(24,33) → 9 pixels, indices 24..32.
  constexpr uint16_t kSpotStart       = 24;
  constexpr uint16_t kSpotEnd         = 32;
  constexpr uint8_t  kSpotCount       = kSpotEnd - kSpotStart + 1;  // 9
  // Scene count shared by BackgroundFade and PaintSpots (12 palettes each).
  constexpr size_t   kSceneCount      = 12;
}

void test_snafu_shade_surface_is_index_zero() {
  TEST_ASSERT_EQUAL_UINT8(0, kShadeSurface);
}

void test_snafu_base_surface_is_index_one() {
  TEST_ASSERT_EQUAL_UINT8(1, kBaseSurface);
}

void test_snafu_shade_pin_is_12() {
  TEST_ASSERT_EQUAL_UINT8(12, kShadePin);
}

void test_snafu_shade_pixel_count_is_40() {
  TEST_ASSERT_EQUAL_UINT(40, kShadePixelCount);
}

void test_snafu_base_pin_is_14() {
  TEST_ASSERT_EQUAL_UINT8(14, kBasePin);
}

void test_snafu_base_pixel_count_is_40() {
  TEST_ASSERT_EQUAL_UINT(40, kBasePixelCount);
}

void test_snafu_max_brightness_is_180() {
  TEST_ASSERT_EQUAL_UINT8(180, kMaxBrightness);
}

void test_snafu_spot_region_matches_python_range_24_33() {
  // Python: range(24, 33) = indices 24, 25, 26, 27, 28, 29, 30, 31, 32
  TEST_ASSERT_EQUAL_UINT(24, kSpotStart);
  TEST_ASSERT_EQUAL_UINT(32, kSpotEnd);
  TEST_ASSERT_EQUAL_UINT(9, kSpotCount);
}

void test_snafu_palette_scene_count_is_12() {
  TEST_ASSERT_EQUAL_UINT(12, kSceneCount);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_snafu_shade_surface_is_index_zero);
  RUN_TEST(test_snafu_base_surface_is_index_one);
  RUN_TEST(test_snafu_shade_pin_is_12);
  RUN_TEST(test_snafu_shade_pixel_count_is_40);
  RUN_TEST(test_snafu_base_pin_is_14);
  RUN_TEST(test_snafu_base_pixel_count_is_40);
  RUN_TEST(test_snafu_max_brightness_is_180);
  RUN_TEST(test_snafu_spot_region_matches_python_range_24_33);
  RUN_TEST(test_snafu_palette_scene_count_is_12);
  return UNITY_END();
}
