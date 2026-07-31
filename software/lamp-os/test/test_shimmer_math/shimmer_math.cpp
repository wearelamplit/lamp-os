#include <unity.h>
#include <vector>

#include "expressions/shimmer/shimmer_math.hpp"
#include "util/color.hpp"

using namespace lamp;

// Deterministic rng stubs: return the midpoint / low / high of every range.
struct MidRng {
  uint32_t range(uint32_t lo, uint32_t hi) { return lo + (hi - lo) / 2; }
};
struct LoRng {
  uint32_t range(uint32_t lo, uint32_t) { return lo; }
};
struct HiRng {
  uint32_t range(uint32_t, uint32_t hi) { return hi; }
};
// Returns a fixed value on every call, scaled into [lo,hi] like a real rng.
struct FixedRng {
  uint32_t v;
  uint32_t range(uint32_t lo, uint32_t hi) {
    return lo + (hi - lo) * v / 1000;
  }
};

void setUp() {}
void tearDown() {}

void test_fire_style_table_values() {
  TEST_ASSERT_EQUAL_FLOAT(0.00f, fireStyle(0).restLevel);   // Twinkle
  TEST_ASSERT_EQUAL_FLOAT(0.0f,  fireStyle(0).windAmp);     // Twinkle: no wind
  TEST_ASSERT_EQUAL_FLOAT(0.12f, fireStyle(1).restLevel);   // Coals (default)
  TEST_ASSERT_EQUAL_FLOAT(0.36f, fireStyle(2).restLevel);   // Candle
  TEST_ASSERT_EQUAL_FLOAT(0.50f, fireStyle(3).restLevel);   // Campfire
}

void test_fire_style_clamps_out_of_range() {
  TEST_ASSERT_EQUAL_FLOAT(fireStyle(3).restLevel, fireStyle(99).restLevel);
}

void test_roll_heat_target_within_bounds_and_clamped() {
  MidRng rng;
  const FireStyle s = fireStyle(2);  // Candle, spark-free
  const float t = rollHeatTarget(s, rng);
  TEST_ASSERT_TRUE(t >= clampUnit(s.restLevel - s.amp) - 1e-4f);
  TEST_ASSERT_TRUE(t <= clampUnit(s.restLevel + s.amp) + 1e-4f);
  MidRng rng2;
  const float hot = rollHeatTarget(fireStyle(3), rng2);
  TEST_ASSERT_TRUE(hot >= 0.0f && hot <= 1.0f);
}

void test_roll_heat_target_spark_hits_hot_band() {
  const FireStyle twinkle = fireStyle(0);  // sparkChance 0.030
  FixedRng belowChance{0};  // prob roll 0.0 < 0.030 -> spark
  const float t = rollHeatTarget(twinkle, belowChance);
  TEST_ASSERT_TRUE(t >= twinkle.sparkLo - 1e-4f);
  TEST_ASSERT_TRUE(t <= twinkle.sparkHi + 1e-4f);
}

void test_roll_heat_target_no_spark_stays_calm() {
  const FireStyle twinkle = fireStyle(0);
  FixedRng aboveChance{999};  // prob roll 0.999 >= 0.030 -> calm band
  const float t = rollHeatTarget(twinkle, aboveChance);
  TEST_ASSERT_TRUE(t >= clampUnit(twinkle.restLevel - twinkle.amp) - 1e-4f);
  TEST_ASSERT_TRUE(t <= clampUnit(twinkle.restLevel + twinkle.amp) + 1e-4f);
}

void test_roll_heat_target_spark_free_styles_unchanged() {
  // sparkChance 0 short-circuits before any rng call: same single roll, same
  // value as the pre-spark calm-band formula, regardless of rng.
  for (uint32_t idx = 2; idx <= 3; ++idx) {
    const FireStyle s = fireStyle(idx);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.sparkChance);
    MidRng mid;
    const float lo = clampUnit(s.restLevel - s.amp);
    const float hi = clampUnit(s.restLevel + s.amp);
    const float expected = lo + (hi - lo) * 0.5f;  // MidRng -> 500/1000
    TEST_ASSERT_EQUAL_FLOAT(expected, rollHeatTarget(s, mid));
    // A spark-forcing rng value can't move them off the calm band.
    FixedRng zero{0};
    TEST_ASSERT_EQUAL_FLOAT(lo, rollHeatTarget(s, zero));
  }
}

void test_roll_wind_target_within_amp_and_boundaries() {
  const FireStyle s = fireStyle(3);  // Campfire, windAmp 0.18
  LoRng lo;
  HiRng hi;
  MidRng mid;
  TEST_ASSERT_EQUAL_FLOAT(-s.windAmp, rollWindTarget(s, lo));  // rng 0 -> -amp
  TEST_ASSERT_EQUAL_FLOAT(s.windAmp, rollWindTarget(s, hi));   // rng 2000 -> +amp
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rollWindTarget(s, mid));       // rng 1000 -> 0
  const float m = rollWindTarget(s, mid);
  TEST_ASSERT_TRUE(m >= -s.windAmp - 1e-4f && m <= s.windAmp + 1e-4f);
}

void test_roll_wind_target_still_air_is_zero() {
  const FireStyle twinkle = fireStyle(0);  // windAmp 0
  LoRng lo;
  HiRng hi;
  MidRng mid;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rollWindTarget(twinkle, lo));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rollWindTarget(twinkle, hi));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rollWindTarget(twinkle, mid));
}

void test_next_gust_at_within_dwell_window() {
  const FireStyle s = fireStyle(2);  // Candle, gust [4500, 9000]
  LoRng lo;
  HiRng hi;
  TEST_ASSERT_EQUAL_UINT32(5000u + s.gustLoMs, nextGustAt(5000u, s, lo));
  TEST_ASSERT_EQUAL_UINT32(5000u + s.gustHiMs, nextGustAt(5000u, s, hi));
}

void test_fire_styles_split_sparkle_and_flame() {
  // Sparkle styles: dark base, spark, no wind.
  const FireStyle twinkle = fireStyle(0);
  const FireStyle coals = fireStyle(1);
  TEST_ASSERT_TRUE(twinkle.sparkChance > 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, twinkle.windAmp);
  TEST_ASSERT_TRUE(twinkle.restLevel < 0.05f);
  TEST_ASSERT_TRUE(coals.sparkChance > 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, coals.windAmp);
  TEST_ASSERT_TRUE(coals.restLevel < 0.30f);
  // Flame styles: no spark, they gust.
  const FireStyle candle = fireStyle(2);
  const FireStyle campfire = fireStyle(3);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, candle.sparkChance);
  TEST_ASSERT_TRUE(candle.windAmp > 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, campfire.sparkChance);
  TEST_ASSERT_TRUE(campfire.windAmp > 0.0f);
}

void test_approach_heat_moves_toward_target() {
  const float a = approachHeat(0.0f, 1.0f, 90, 180);   // half a step
  TEST_ASSERT_TRUE(a > 0.0f && a < 1.0f);
  const float full = approachHeat(0.0f, 1.0f, 500, 180);  // dt > step, cap at target
  TEST_ASSERT_EQUAL_FLOAT(1.0f, full);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, approachHeat(0.3f, 1.0f, 10, 0));  // stepMs 0 snaps
}

void test_heat_brightness_monotonic_with_floor() {
  TEST_ASSERT_EQUAL_FLOAT(0.2f, heatBrightness(0.0f, 0.2f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, heatBrightness(1.0f, 0.2f));
  TEST_ASSERT_TRUE(heatBrightness(0.5f, 0.2f) > heatBrightness(0.1f, 0.2f));
}

void test_sample_gradient_endpoints_and_mid() {
  std::vector<Color> stops = {Color{0, 0, 0, 0}, Color{100, 0, 0, 0},
                              Color{200, 0, 0, 0}};
  TEST_ASSERT_EQUAL_UINT8(0,   sampleGradient(stops, 0.0f).r);
  TEST_ASSERT_EQUAL_UINT8(200, sampleGradient(stops, 1.0f).r);
  const uint8_t mid = sampleGradient(stops, 0.5f).r;
  TEST_ASSERT_TRUE(mid > 80 && mid < 120);
}

void test_sample_gradient_four_stops() {
  // Descriptor allows colors.max = 4; sample across all three segments.
  std::vector<Color> stops = {Color{0, 0, 0, 0}, Color{90, 0, 0, 0},
                              Color{180, 0, 0, 0}, Color{255, 0, 0, 0}};
  TEST_ASSERT_EQUAL_UINT8(0,   sampleGradient(stops, 0.0f).r);
  TEST_ASSERT_EQUAL_UINT8(255, sampleGradient(stops, 1.0f).r);
  TEST_ASSERT_EQUAL_UINT8(90,  sampleGradient(stops, 1.0f / 3.0f).r);
  TEST_ASSERT_EQUAL_UINT8(180, sampleGradient(stops, 2.0f / 3.0f).r);
  const uint8_t mid = sampleGradient(stops, 0.5f).r;
  TEST_ASSERT_TRUE(mid > 90 && mid < 180);  // between stops 1 and 2
}

void test_sample_gradient_degenerate() {
  TEST_ASSERT_EQUAL_UINT8(0, sampleGradient({}, 0.5f).r);
  std::vector<Color> one = {Color{55, 0, 0, 0}};
  TEST_ASSERT_EQUAL_UINT8(55, sampleGradient(one, 0.5f).r);
  // Clamp out-of-range p.
  TEST_ASSERT_EQUAL_UINT8(55, sampleGradient(one, 9.0f).r);
}

void test_default_fire_ramp_is_warm() {
  const auto& ramp = defaultFireRamp();
  TEST_ASSERT_TRUE(ramp.size() >= 2);
  TEST_ASSERT_TRUE(ramp.back().r >= ramp.front().r);  // hotter end brighter red
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fire_style_table_values);
  RUN_TEST(test_fire_style_clamps_out_of_range);
  RUN_TEST(test_roll_heat_target_within_bounds_and_clamped);
  RUN_TEST(test_roll_heat_target_spark_hits_hot_band);
  RUN_TEST(test_roll_heat_target_no_spark_stays_calm);
  RUN_TEST(test_roll_heat_target_spark_free_styles_unchanged);
  RUN_TEST(test_roll_wind_target_within_amp_and_boundaries);
  RUN_TEST(test_roll_wind_target_still_air_is_zero);
  RUN_TEST(test_next_gust_at_within_dwell_window);
  RUN_TEST(test_fire_styles_split_sparkle_and_flame);
  RUN_TEST(test_approach_heat_moves_toward_target);
  RUN_TEST(test_heat_brightness_monotonic_with_floor);
  RUN_TEST(test_sample_gradient_endpoints_and_mid);
  RUN_TEST(test_sample_gradient_four_stops);
  RUN_TEST(test_sample_gradient_degenerate);
  RUN_TEST(test_default_fire_ramp_is_warm);
  return UNITY_END();
}
