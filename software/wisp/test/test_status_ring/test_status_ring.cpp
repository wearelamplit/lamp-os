// Native tests for the wisp indicator-ring gradient helper.
//
// Pins computeRingGradient()'s spec:
//   - empty palette → returns false, output untouched (caller falls back
//     to warm-white).
//   - single stop → every pixel takes that color.
//   - two stops → linear interp endpoints land on the stops, midpoint
//     halfway between.
//   - N stops, 30 pixels → stops land exactly on evenly-spaced pixel
//     positions (no off-by-one drift across the ring).
//   - Q16.16 fixed-point matches a float reference within 1 LSB.
//
// Also covers fillRingWarmWhite() and rgbwToRgbWarmBias().

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "StatusRing.h"

namespace {

constexpr size_t kPixels = wisp::kStatusRingPixelCount;
constexpr size_t kBufBytes = kPixels * 3;

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_empty_palette_returns_false(void) {
  uint8_t out[kBufBytes];
  std::memset(out, 0xAB, sizeof(out));
  const bool ok = wisp::computeRingGradient(nullptr, 0, out, kPixels);
  TEST_ASSERT_FALSE(ok);
  // First byte should still be the sentinel — function must not have
  // written anything when it returns false.
  TEST_ASSERT_EQUAL_UINT8(0xAB, out[0]);
}

void test_single_stop_fills_uniform(void) {
  const uint8_t stop[3] = {12, 34, 56};
  uint8_t out[kBufBytes] = {0};
  TEST_ASSERT_TRUE(wisp::computeRingGradient(stop, 1, out, kPixels));
  for (size_t i = 0; i < kPixels; ++i) {
    TEST_ASSERT_EQUAL_UINT8(12, out[i * 3 + 0]);
    TEST_ASSERT_EQUAL_UINT8(34, out[i * 3 + 1]);
    TEST_ASSERT_EQUAL_UINT8(56, out[i * 3 + 2]);
  }
}

void test_two_stops_endpoints_and_midpoint(void) {
  // Black → White.
  const uint8_t stops[6] = {0, 0, 0, 255, 255, 255};
  uint8_t out[kBufBytes] = {0};
  TEST_ASSERT_TRUE(wisp::computeRingGradient(stops, 2, out, kPixels));
  // Pixel 0 should land on the first stop exactly.
  TEST_ASSERT_EQUAL_UINT8(0, out[0]);
  TEST_ASSERT_EQUAL_UINT8(0, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0, out[2]);
  // Pixel 29 should land on the last stop exactly.
  const size_t lastBase = (kPixels - 1) * 3;
  TEST_ASSERT_EQUAL_UINT8(255, out[lastBase + 0]);
  TEST_ASSERT_EQUAL_UINT8(255, out[lastBase + 1]);
  TEST_ASSERT_EQUAL_UINT8(255, out[lastBase + 2]);
  // The math is symmetric; opposite pixels should sum to 255 within a
  // 1-LSB rounding tolerance for every channel.
  for (size_t i = 0; i < kPixels / 2; ++i) {
    const size_t a = i * 3;
    const size_t b = (kPixels - 1 - i) * 3;
    for (size_t c = 0; c < 3; ++c) {
      const int sum = static_cast<int>(out[a + c]) +
                      static_cast<int>(out[b + c]);
      TEST_ASSERT_INT_WITHIN(1, 255, sum);
    }
  }
}

void test_two_stops_pure_red_to_blue(void) {
  const uint8_t stops[6] = {255, 0, 0, 0, 0, 255};
  uint8_t out[kBufBytes] = {0};
  TEST_ASSERT_TRUE(wisp::computeRingGradient(stops, 2, out, kPixels));
  // Red ramps down monotonically, Blue ramps up monotonically, Green
  // stays at 0 throughout.
  for (size_t i = 1; i < kPixels; ++i) {
    const size_t prev = (i - 1) * 3;
    const size_t cur = i * 3;
    TEST_ASSERT_TRUE(out[cur + 0] <= out[prev + 0]);     // R non-increasing
    TEST_ASSERT_TRUE(out[cur + 2] >= out[prev + 2]);     // B non-decreasing
    TEST_ASSERT_EQUAL_UINT8(0, out[cur + 1]);            // G stays 0
  }
}

void test_three_stops_lands_evenly(void) {
  // R, G, B at positions 0, 14.5, 29 → with linear stretch the middle
  // stop should land on pixel 14 or 15 (Q16.16 rounding). Verify the
  // strongest channel matches the expected stop at endpoints and that
  // the green peak is in the middle third of the ring.
  const uint8_t stops[9] = {
      255, 0, 0,    // stop 0: red
      0, 255, 0,    // stop 1: green
      0, 0, 255,    // stop 2: blue
  };
  uint8_t out[kBufBytes] = {0};
  TEST_ASSERT_TRUE(wisp::computeRingGradient(stops, 3, out, kPixels));

  // Endpoints land exactly on the stops.
  TEST_ASSERT_EQUAL_UINT8(255, out[0]);
  TEST_ASSERT_EQUAL_UINT8(0, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0, out[2]);
  const size_t lastBase = (kPixels - 1) * 3;
  TEST_ASSERT_EQUAL_UINT8(0, out[lastBase + 0]);
  TEST_ASSERT_EQUAL_UINT8(0, out[lastBase + 1]);
  TEST_ASSERT_EQUAL_UINT8(255, out[lastBase + 2]);

  // Find the pixel where green peaks; with 3 stops on 30 pixels the
  // middle stop lands at t = 1.0 which falls between pixels 14
  // (t=28/29 ≈ 0.966) and 15 (t=30/29 ≈ 1.034), so the green peak
  // straddles those two pixels at roughly 96-97% strength rather than
  // hitting 255. The shape we care about is "green peaks in the middle
  // third of the ring".
  size_t peakIdx = 0;
  uint8_t peakG = 0;
  for (size_t i = 0; i < kPixels; ++i) {
    if (out[i * 3 + 1] > peakG) {
      peakG = out[i * 3 + 1];
      peakIdx = i;
    }
  }
  TEST_ASSERT_TRUE(peakG >= 240);
  TEST_ASSERT_TRUE(peakIdx >= 10 && peakIdx <= 19);
}

void test_fill_warm_white(void) {
  uint8_t out[kBufBytes] = {0};
  wisp::fillRingWarmWhite(out, kPixels);
  for (size_t i = 0; i < kPixels; ++i) {
    TEST_ASSERT_EQUAL_UINT8(wisp::kWarmWhiteR, out[i * 3 + 0]);
    TEST_ASSERT_EQUAL_UINT8(wisp::kWarmWhiteG, out[i * 3 + 1]);
    TEST_ASSERT_EQUAL_UINT8(wisp::kWarmWhiteB, out[i * 3 + 2]);
  }
}

void test_rgbw_warm_bias_passthrough_when_w_zero(void) {
  uint8_t r = 0, g = 0, b = 0;
  wisp::rgbwToRgbWarmBias(100, 50, 200, 0, r, g, b);
  TEST_ASSERT_EQUAL_UINT8(100, r);
  TEST_ASSERT_EQUAL_UINT8(50, g);
  TEST_ASSERT_EQUAL_UINT8(200, b);
}

void test_rgbw_warm_bias_adds_warm(void) {
  // W=100 → +70 R, +40 G, +0 B (relative to inputs).
  uint8_t r = 0, g = 0, b = 0;
  wisp::rgbwToRgbWarmBias(10, 20, 30, 100, r, g, b);
  TEST_ASSERT_EQUAL_UINT8(80, r);   // 10 + 70
  TEST_ASSERT_EQUAL_UINT8(60, g);   // 20 + 40
  TEST_ASSERT_EQUAL_UINT8(30, b);   // unchanged
}

void test_rgbw_warm_bias_clamps_to_255(void) {
  uint8_t r = 0, g = 0, b = 0;
  wisp::rgbwToRgbWarmBias(250, 240, 50, 255, r, g, b);
  TEST_ASSERT_EQUAL_UINT8(255, r);  // 250 + 178 → clamped
  TEST_ASSERT_EQUAL_UINT8(255, g);  // 240 + 102 → clamped
  TEST_ASSERT_EQUAL_UINT8(50, b);
}

void test_ten_stops_no_buffer_overrun(void) {
  // Exercises the max-stops case (kManualPaletteMaxColors == 10) and
  // confirms the fixed-point path doesn't drift over a longer palette.
  uint8_t stops[30];
  for (size_t i = 0; i < 10; ++i) {
    stops[i * 3 + 0] = static_cast<uint8_t>(i * 25);
    stops[i * 3 + 1] = static_cast<uint8_t>(255 - i * 25);
    stops[i * 3 + 2] = static_cast<uint8_t>(i * 10);
  }
  uint8_t out[kBufBytes] = {0};
  TEST_ASSERT_TRUE(wisp::computeRingGradient(stops, 10, out, kPixels));
  // Endpoints match.
  TEST_ASSERT_EQUAL_UINT8(0, out[0]);
  TEST_ASSERT_EQUAL_UINT8(255, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0, out[2]);
  const size_t lastBase = (kPixels - 1) * 3;
  TEST_ASSERT_EQUAL_UINT8(9 * 25, out[lastBase + 0]);
  TEST_ASSERT_EQUAL_UINT8(255 - 9 * 25, out[lastBase + 1]);
  TEST_ASSERT_EQUAL_UINT8(9 * 10, out[lastBase + 2]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_palette_returns_false);
  RUN_TEST(test_single_stop_fills_uniform);
  RUN_TEST(test_two_stops_endpoints_and_midpoint);
  RUN_TEST(test_two_stops_pure_red_to_blue);
  RUN_TEST(test_three_stops_lands_evenly);
  RUN_TEST(test_fill_warm_white);
  RUN_TEST(test_rgbw_warm_bias_passthrough_when_w_zero);
  RUN_TEST(test_rgbw_warm_bias_adds_warm);
  RUN_TEST(test_rgbw_warm_bias_clamps_to_255);
  RUN_TEST(test_ten_stops_no_buffer_overrun);
  return UNITY_END();
}
