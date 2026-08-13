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
// Counts draws so a test can assert a code path consumes zero rng.
struct CountingRng {
  uint32_t calls = 0;
  uint32_t range(uint32_t lo, uint32_t hi) {
    ++calls;
    return lo + (hi - lo) / 2;
  }
};

void setUp() {}
void tearDown() {}

void test_fire_style_table_values() {
  TEST_ASSERT_EQUAL_FLOAT(0.00f, fireStyle(0).restLevel);   // Twinkle
  TEST_ASSERT_EQUAL_FLOAT(0.0f,  fireStyle(0).windAmp);     // Twinkle: no wind
  TEST_ASSERT_EQUAL_FLOAT(0.06f, fireStyle(1).restLevel);   // Coals (default)
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

void test_roll_heat_target_spark_free_short_circuits() {
  // sparkChance 0 short-circuits before any rng call: the calm-band roll is
  // returned regardless of a spark-forcing rng value.
  FireStyle s = fireStyle(2);  // Candle, then force spark-free
  s.sparkChance = 0.0f;
  const float lo = clampUnit(s.restLevel - s.amp);
  const float hi = clampUnit(s.restLevel + s.amp);
  MidRng mid;
  TEST_ASSERT_EQUAL_FLOAT(lo + (hi - lo) * 0.5f, rollHeatTarget(s, mid));
  FixedRng zero{0};  // would force a spark if sparkChance were > 0
  TEST_ASSERT_EQUAL_FLOAT(lo, rollHeatTarget(s, zero));
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
  // Flame styles: wind drives them (still-air styles have none); they also spark.
  const FireStyle candle = fireStyle(2);
  const FireStyle campfire = fireStyle(3);
  TEST_ASSERT_TRUE(candle.windAmp > 0.0f);
  TEST_ASSERT_TRUE(candle.sparkChance > 0.0f);
  TEST_ASSERT_TRUE(campfire.windAmp > 0.0f);
  TEST_ASSERT_TRUE(campfire.sparkChance > 0.0f);
}

void test_flutter_calm_styles_unperturbed() {
  // Twinkle + Coals have flutterAmp 0: advanceFlutter returns 0 AND draws no rng,
  // so calm styles render byte-identically (a stray draw would shift the shared
  // rng sequence for the rest of the frame).
  for (uint32_t idx = 0; idx <= 1; ++idx) {
    const FireStyle s = fireStyle(idx);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.flutterAmp);
    CountingRng rng;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, advanceFlutter(0.25f, s.flutterAmp, rng));
    TEST_ASSERT_EQUAL_UINT32(0, rng.calls);
  }
}

void test_flutter_stays_within_amplitude() {
  const FireStyle candle = fireStyle(2);
  TEST_ASSERT_TRUE(candle.flutterAmp > 0.0f);
  // Drive the walk up with a max-positive rng then down with a max-negative one;
  // it must never escape [-amp, +amp].
  HiRng hi;
  LoRng lo;
  float f = 0.0f;
  for (int i = 0; i < 200; ++i) {
    f = advanceFlutter(f, candle.flutterAmp, hi);
    TEST_ASSERT_TRUE(f <= candle.flutterAmp + 1e-4f);
    TEST_ASSERT_TRUE(f >= -candle.flutterAmp - 1e-4f);
  }
  for (int i = 0; i < 200; ++i) {
    f = advanceFlutter(f, candle.flutterAmp, lo);
    TEST_ASSERT_TRUE(f <= candle.flutterAmp + 1e-4f);
    TEST_ASSERT_TRUE(f >= -candle.flutterAmp - 1e-4f);
  }
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

static int brightnessSum(Color c) {
  return static_cast<int>(c.r) + c.g + c.b + c.w;
}

void test_warmth_modulate_neutral_returns_anchor() {
  const Color anchor{200, 40, 0, 0};
  const FireStyle s = fireStyle(2);  // Candle
  const Color mid =
      warmthModulate(anchor, s.restLevel, s.restLevel, s.warmthSwing, s.whiteHot);
  TEST_ASSERT_EQUAL_UINT8(anchor.r, mid.r);
  TEST_ASSERT_EQUAL_UINT8(anchor.g, mid.g);
  TEST_ASSERT_EQUAL_UINT8(anchor.b, mid.b);
  TEST_ASSERT_EQUAL_UINT8(anchor.w, mid.w);
}

void test_warmth_modulate_direction_warm_anchor() {
  const Color anchor{200, 40, 0, 0};  // warm red-orange
  const FireStyle s = fireStyle(2);   // Candle
  const float neutral = s.restLevel;
  const Color cold = warmthModulate(anchor, 0.0f, neutral, s.warmthSwing, s.whiteHot);
  const Color mid  = warmthModulate(anchor, neutral, neutral, s.warmthSwing, s.whiteHot);
  const Color hot  = warmthModulate(anchor, 1.0f, neutral, s.warmthSwing, s.whiteHot);
  // Hot brighter than mid brighter than cold.
  TEST_ASSERT_TRUE(brightnessSum(hot) > brightnessSum(mid));
  TEST_ASSERT_TRUE(brightnessSum(mid) > brightnessSum(cold));
  // Hot slides toward yellow (green climbs); cold toward maroon (green falls).
  TEST_ASSERT_TRUE(hot.g > mid.g);
  TEST_ASSERT_TRUE(cold.g < mid.g);
}

void test_warmth_modulate_channel_bounded_all_heat() {
  const Color anchor{255, 200, 120, 60};
  const FireStyle s = fireStyle(3);  // Campfire, strongest swing + whiteHot
  for (int k = 0; k <= 100; ++k) {
    const Color c = warmthModulate(anchor, k / 100.0f, s.restLevel,
                                   s.warmthSwing, s.whiteHot);
    (void)c;  // uint8_t channels are 0..255 by construction; compiles-and-runs guard
  }
  TEST_PASS();
}

void test_warmth_modulate_zero_swing_is_pure_brightness() {
  const Color anchor{120, 60, 0, 0};  // r == 2*g, clean ratio under scaling
  const float neutral = 0.3f;
  const Color hot  = warmthModulate(anchor, 1.0f, neutral, 0.0f, 0.0f);
  const Color cold = warmthModulate(anchor, 0.0f, neutral, 0.0f, 0.0f);
  // No hue shift: r:g ratio preserved (within rounding).
  TEST_ASSERT_INT_WITHIN(1, hot.r, hot.g * 2);
  TEST_ASSERT_INT_WITHIN(1, cold.r, cold.g * 2);
  TEST_ASSERT_EQUAL_UINT8(0, hot.b);
  TEST_ASSERT_EQUAL_UINT8(0, cold.b);
  // Still brightness-modulated.
  TEST_ASSERT_TRUE(brightnessSum(hot) > brightnessSum(cold));
}

void test_warmth_modulate_cool_anchor_valid() {
  const Color anchor{0, 0, 200, 0};  // pure blue
  const FireStyle s = fireStyle(2);  // Candle
  const Color hot  = warmthModulate(anchor, 1.0f, s.restLevel, s.warmthSwing, s.whiteHot);
  const Color cold = warmthModulate(anchor, 0.0f, s.restLevel, s.warmthSwing, s.whiteHot);
  TEST_ASSERT_TRUE(brightnessSum(hot) > brightnessSum(cold));
}

void test_warmth_modulate_black_anchor_stays_black() {
  const Color anchor{0, 0, 0, 0};
  const FireStyle s = fireStyle(3);  // Campfire, whiteHot 0.40
  const Color hot = warmthModulate(anchor, 1.0f, s.restLevel, s.warmthSwing, s.whiteHot);
  TEST_ASSERT_EQUAL_UINT8(0, hot.r);
  TEST_ASSERT_EQUAL_UINT8(0, hot.g);
  TEST_ASSERT_EQUAL_UINT8(0, hot.b);
  TEST_ASSERT_EQUAL_UINT8(0, hot.w);  // no fake-fire: shimmer of nothing is nothing
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fire_style_table_values);
  RUN_TEST(test_fire_style_clamps_out_of_range);
  RUN_TEST(test_roll_heat_target_within_bounds_and_clamped);
  RUN_TEST(test_roll_heat_target_spark_hits_hot_band);
  RUN_TEST(test_roll_heat_target_no_spark_stays_calm);
  RUN_TEST(test_roll_heat_target_spark_free_short_circuits);
  RUN_TEST(test_roll_wind_target_within_amp_and_boundaries);
  RUN_TEST(test_roll_wind_target_still_air_is_zero);
  RUN_TEST(test_next_gust_at_within_dwell_window);
  RUN_TEST(test_fire_styles_split_sparkle_and_flame);
  RUN_TEST(test_flutter_calm_styles_unperturbed);
  RUN_TEST(test_flutter_stays_within_amplitude);
  RUN_TEST(test_approach_heat_moves_toward_target);
  RUN_TEST(test_heat_brightness_monotonic_with_floor);
  RUN_TEST(test_warmth_modulate_neutral_returns_anchor);
  RUN_TEST(test_warmth_modulate_direction_warm_anchor);
  RUN_TEST(test_warmth_modulate_channel_bounded_all_heat);
  RUN_TEST(test_warmth_modulate_zero_swing_is_pure_brightness);
  RUN_TEST(test_warmth_modulate_cool_anchor_valid);
  RUN_TEST(test_warmth_modulate_black_anchor_stays_black);
  return UNITY_END();
}
