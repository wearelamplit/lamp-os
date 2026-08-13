// Native tests for the power-governor current estimator. The reference rows
// pin the led-common constants: a drifted constant fails the ±2 clamp checks.

#include <unity.h>

#include <cstdint>
#include <vector>

#include "../../src/core/power_governor.cpp"

using lamp::Color;
using lamp::demandMa;
using lamp::fullDutyMa;
using lampos::led::kIdleMaPerPixel;
using lampos::led::kMaPerChannelFullDuty;

void setUp() {}
void tearDown() {}

static float channelMa(uint8_t v) {
  return Adafruit_NeoPixel::gamma8(v) / 255.0f * kMaPerChannelFullDuty;
}

void test_fullduty_matches_manual_gamma_sum_rgbw() {
  const std::vector<Color> buf = {
      {255, 128, 64, 32}, {0, 1, 2, 3}, {10, 20, 30, 40}, {255, 255, 255, 255}};
  float expected = 0.0f;
  for (const auto& c : buf) {
    expected += channelMa(c.r) + channelMa(c.g) + channelMa(c.b) + channelMa(c.w);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, expected, fullDutyMa(buf, 4));
}

void test_three_channel_excludes_w() {
  const std::vector<Color> buf = {{255, 128, 64, 255}, {0, 0, 0, 255}};
  float expected = 0.0f;
  for (const auto& c : buf) {
    expected += channelMa(c.r) + channelMa(c.g) + channelMa(c.b);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, expected, fullDutyMa(buf, 3));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2.0f * kMaPerChannelFullDuty,
                           fullDutyMa(buf, 4) - fullDutyMa(buf, 3));
}

void test_demand_brightness_scaler_matches_neopixel() {
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1000.0f, demandMa(1000.0f, 255, 0));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1000.0f * 128.0f / 256.0f,
                           demandMa(1000.0f, 127, 0));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1000.0f / 256.0f, demandMa(1000.0f, 0, 0));
}

void test_idle_term_black_buffer() {
  const std::vector<Color> buf(64);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, fullDutyMa(buf, 4));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 64.0f * kIdleMaPerPixel,
                           demandMa(fullDutyMa(buf, 4), 255, 64));
}

// Design point: 64 px RGBW, 2 A supply, quiet reserve 200 → pixel budget 1800.
// Clamp level solves demandMa(sum, fit, 64) == budget, the governor's fit.
static float clampFor(const Color& c, uint8_t channels) {
  const std::vector<Color> buf(64, c);
  const float idle = 64.0f * kIdleMaPerPixel;
  return 256.0f * (1800.0f - idle) / fullDutyMa(buf, channels) - 1.0f;
}

void test_reference_vivid_two_channel_clamp() {
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 350.0f, clampFor({255, 0, 255, 0}, 4));
}

void test_reference_three_channel_white_clamp() {
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 233.0f, clampFor({255, 255, 255, 0}, 3));
}

void test_reference_four_channel_full_clamp() {
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 174.5f, clampFor({255, 255, 255, 255}, 4));
}

// Whole-lamp draw anchors: each role is treated as its pixel count drawing the
// role palette average; both role terms sum. Pinned to a snafu-shaped layout
// (24 px base + 37 px shade) so dropping the shade term, or a drift in
// kMaPerChannelFullDuty (10) / kIdleMaPerPixel (0.7), fails here.
void test_draw_anchors_base_plus_shade() {
  const std::vector<Color> basePalette = {{255, 0, 255, 0}, {0, 0, 0, 0}};
  const std::vector<Color> shadePalette = {{0, 0, 0, 255}};
  const uint16_t basePx = 24, shadePx = 37;
  const float baseAvg = fullDutyMa(basePalette, 4) / basePalette.size();    // 10
  const float shadeAvg = fullDutyMa(shadePalette, 4) / shadePalette.size();  // 10
  const float sum = baseAvg * basePx + shadeAvg * shadePx;  // 240 + 370 = 610
  const uint16_t totalPx = basePx + shadePx;                // 61
  const float idle = demandMa(sum, 0, totalPx);             // 610/256 + 42.7
  const float full = demandMa(sum, 255, totalPx);           // 610 + 42.7
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 240.0f, baseAvg * basePx);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 370.0f, shadeAvg * shadePx);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 610.0f, sum);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 45.082812f, idle);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 652.7f, full);
  TEST_ASSERT_EQUAL_UINT16(45, static_cast<uint16_t>(idle));
  TEST_ASSERT_EQUAL_UINT16(652, static_cast<uint16_t>(full));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_fullduty_matches_manual_gamma_sum_rgbw);
  RUN_TEST(test_three_channel_excludes_w);
  RUN_TEST(test_demand_brightness_scaler_matches_neopixel);
  RUN_TEST(test_idle_term_black_buffer);
  RUN_TEST(test_reference_vivid_two_channel_clamp);
  RUN_TEST(test_reference_three_channel_white_clamp);
  RUN_TEST(test_reference_four_channel_full_clamp);
  RUN_TEST(test_draw_anchors_base_plus_shade);
  return UNITY_END();
}
