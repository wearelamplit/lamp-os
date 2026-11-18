#include <unity.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "expressions/shimmer/shimmer_math.hpp"
#include "util/color.hpp"
#include "util/fast_rng.hpp"

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
  const FireStyle s = fireStyle(2);  // Candle
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
    Flutter f{0.25f, 0.25f, 0};
    TEST_ASSERT_EQUAL_FLOAT(
        0.0f, advanceFlutter(f, s.flutterAmp, s.flutterStepMs, 16, rng));
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
  Flutter f;
  for (int i = 0; i < 400; ++i) {
    const float v = advanceFlutter(f, candle.flutterAmp, candle.flutterStepMs,
                                   16, hi);
    TEST_ASSERT_TRUE(v <= candle.flutterAmp + 1e-4f);
    TEST_ASSERT_TRUE(v >= -candle.flutterAmp - 1e-4f);
  }
  for (int i = 0; i < 400; ++i) {
    const float v = advanceFlutter(f, candle.flutterAmp, candle.flutterStepMs,
                                   16, lo);
    TEST_ASSERT_TRUE(v <= candle.flutterAmp + 1e-4f);
    TEST_ASSERT_TRUE(v >= -candle.flutterAmp - 1e-4f);
  }
}

// Runs a flutter at a fixed frame period and records the shown value at every
// multiple of sampleMs, over totalMs of wall time.
static std::vector<float> flutterTrace(float amp, uint32_t stepMs,
                                       uint32_t frameMs, uint32_t sampleMs,
                                       uint32_t totalMs) {
  FastRng rng(12345);
  Flutter f;
  std::vector<float> out;
  for (uint32_t t = frameMs; t <= totalMs; t += frameMs) {
    const float v = advanceFlutter(f, amp, stepMs, frameMs, rng);
    if (t % sampleMs == 0) out.push_back(v);
  }
  return out;
}

void test_flutter_is_frame_rate_independent() {
  // 33 ms (~30 fps) and 8 ms (125 fps) frames over the same wall time, sampled
  // where both land on a frame: the same seed shows the same value, for every
  // flame style's step period.
  for (uint32_t idx = 2; idx <= 3; ++idx) {
    const FireStyle s = fireStyle(idx);
    const auto slow = flutterTrace(s.flutterAmp, s.flutterStepMs, 33, 264, 26400);
    const auto fast = flutterTrace(s.flutterAmp, s.flutterStepMs, 8, 264, 26400);
    TEST_ASSERT_EQUAL_UINT32(100, slow.size());
    TEST_ASSERT_EQUAL_UINT32(slow.size(), fast.size());
    for (size_t i = 0; i < slow.size(); ++i) {
      TEST_ASSERT_FLOAT_WITHIN(1e-5f, slow[i], fast[i]);
    }
  }
}

void test_flutter_zero_delta_holds() {
  FastRng rng(7);
  CountingRng counter;
  Flutter f;
  const FireStyle s = fireStyle(2);
  const float a = advanceFlutter(f, s.flutterAmp, s.flutterStepMs, 130, rng);
  const Flutter before = f;
  const float b = advanceFlutter(f, s.flutterAmp, s.flutterStepMs, 0, counter);
  TEST_ASSERT_EQUAL_FLOAT(a, b);
  TEST_ASSERT_EQUAL_UINT32(0, counter.calls);
  TEST_ASSERT_EQUAL_UINT32(before.carryMs, f.carryMs);
}

void test_flutter_large_delta_bounded() {
  // A stall of any length catches up at most kFlutterMaxCatchUpMs of steps,
  // stays in band and leaves less than one step pending.
  const FireStyle s = fireStyle(3);
  CountingRng rng;
  Flutter f;
  const float v = advanceFlutter(f, s.flutterAmp, s.flutterStepMs, 0xFFFFFFFFu, rng);
  TEST_ASSERT_EQUAL_UINT32(kFlutterMaxCatchUpMs / s.flutterStepMs, rng.calls);
  TEST_ASSERT_TRUE(f.carryMs < s.flutterStepMs);
  TEST_ASSERT_TRUE(v <= s.flutterAmp + 1e-4f && v >= -s.flutterAmp - 1e-4f);
}

// Flutter brightness travel per second, at 60 fps over 10 s.
static float flutterTravelPerSecond(const FireStyle& s) {
  FastRng rng(99);
  Flutter f;
  float prev = 0.0f;
  float travel = 0.0f;
  for (uint32_t t = 0; t < 10000; t += 16) {
    const float v = advanceFlutter(f, s.flutterAmp, s.flutterStepMs, 16, rng);
    travel += std::fabs(v - prev);
    prev = v;
  }
  return travel / 10.0f;
}

void test_candle_flutter_travel_per_second_bounded() {
  TEST_ASSERT_TRUE(flutterTravelPerSecond(fireStyle(2)) <= 0.20f);
}

void test_candle_heat_closes_at_most_4pct_per_frame() {
  TEST_ASSERT_TRUE(approachFactor(16, fireStyle(2).stepMs) <= 0.04f);
}

void test_campfire_flutter_travel_per_second_bounded() {
  TEST_ASSERT_TRUE(flutterTravelPerSecond(fireStyle(3)) <= 0.15f);
}

void test_campfire_heat_closes_at_most_6pct_per_frame() {
  TEST_ASSERT_TRUE(approachFactor(16, fireStyle(3).stepMs) <= 0.06f);
}

void test_flutter_glides_between_steps() {
  // Halfway through a step the shown value is halfway between the two points.
  CountingRng rng;
  Flutter f{0.0f, 0.02f, 40};
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.01f, advanceFlutter(f, 0.04f, 80, 0, rng));
  TEST_ASSERT_EQUAL_UINT32(0, rng.calls);
}

void test_flutter_frame_moves_at_most_its_share_of_a_step() {
  // At 16 ms frames each frame moves at most step * 16 / flutterStepMs.
  for (uint32_t idx = 2; idx <= 3; ++idx) {
    const FireStyle s = fireStyle(idx);
    const float maxMove =
        s.flutterAmp * kFlutterStepFrac * 16.0f / s.flutterStepMs + 1e-5f;
    FastRng rng(4242);
    Flutter f;
    float prev = advanceFlutter(f, s.flutterAmp, s.flutterStepMs, 0, rng);
    for (int i = 0; i < 2000; ++i) {
      const float v = advanceFlutter(f, s.flutterAmp, s.flutterStepMs, 16, rng);
      TEST_ASSERT_TRUE(std::fabs(v - prev) <= maxMove);
      prev = v;
    }
  }
}

void test_calm_styles_pinned() {
  const FireStyle t = fireStyle(0);  // Twinkle
  TEST_ASSERT_EQUAL_FLOAT(0.00f, t.restLevel);
  TEST_ASSERT_EQUAL_FLOAT(0.02f, t.amp);
  TEST_ASSERT_EQUAL_UINT32(900, t.stepMs);
  TEST_ASSERT_EQUAL_FLOAT(0.00f, t.windAmp);
  TEST_ASSERT_EQUAL_UINT32(0, t.gustLoMs);
  TEST_ASSERT_EQUAL_UINT32(0, t.gustHiMs);
  TEST_ASSERT_EQUAL_FLOAT(0.030f, t.sparkChance);
  TEST_ASSERT_EQUAL_FLOAT(0.60f, t.sparkLo);
  TEST_ASSERT_EQUAL_FLOAT(1.00f, t.sparkHi);
  TEST_ASSERT_EQUAL_FLOAT(0.00f, t.flutterAmp);
  TEST_ASSERT_EQUAL_UINT32(0, t.flutterStepMs);
  TEST_ASSERT_EQUAL_FLOAT(0.15f, t.warmthSwing);
  TEST_ASSERT_EQUAL_FLOAT(0.00f, t.whiteHot);
  TEST_ASSERT_EQUAL_FLOAT(0.80f, t.minBright);
  const FireStyle c = fireStyle(1);  // Coals
  TEST_ASSERT_EQUAL_FLOAT(0.06f, c.restLevel);
  TEST_ASSERT_EQUAL_FLOAT(0.05f, c.amp);
  TEST_ASSERT_EQUAL_UINT32(1600, c.stepMs);
  TEST_ASSERT_EQUAL_FLOAT(0.00f, c.windAmp);
  TEST_ASSERT_EQUAL_UINT32(0, c.gustLoMs);
  TEST_ASSERT_EQUAL_UINT32(0, c.gustHiMs);
  TEST_ASSERT_EQUAL_FLOAT(0.045f, c.sparkChance);
  TEST_ASSERT_EQUAL_FLOAT(0.55f, c.sparkLo);
  TEST_ASSERT_EQUAL_FLOAT(0.85f, c.sparkHi);
  TEST_ASSERT_EQUAL_FLOAT(0.00f, c.flutterAmp);
  TEST_ASSERT_EQUAL_UINT32(0, c.flutterStepMs);
  TEST_ASSERT_EQUAL_FLOAT(0.30f, c.warmthSwing);
  TEST_ASSERT_EQUAL_FLOAT(0.00f, c.whiteHot);
  TEST_ASSERT_EQUAL_FLOAT(0.40f, c.minBright);
}

void test_liveliness_order() {
  // Coals < Candle < Campfire: faster heat easing, faster flutter, deeper wind.
  TEST_ASSERT_TRUE(fireStyle(1).stepMs > fireStyle(2).stepMs);
  TEST_ASSERT_TRUE(fireStyle(2).stepMs > fireStyle(3).stepMs);
  TEST_ASSERT_TRUE(fireStyle(2).flutterStepMs > fireStyle(3).flutterStepMs);
  TEST_ASSERT_TRUE(fireStyle(2).windAmp < fireStyle(3).windAmp);
}

void test_approach_heat_moves_toward_target() {
  const float a = approachHeat(0.0f, 1.0f, approachFactor(90, 180));
  TEST_ASSERT_TRUE(a > 0.0f && a < 1.0f);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, approachHeat(0.3f, 1.0f, approachFactor(10, 0)));
}

void test_approach_frame_rate_independent() {
  // Two 8 ms draws land where one 16 ms draw does.
  const float k8 = approachFactor(8, 220);
  const float twice = approachHeat(approachHeat(0.0f, 1.0f, k8), 1.0f, k8);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, approachHeat(0.0f, 1.0f, approachFactor(16, 220)),
                           twice);
}

void test_approach_zero_and_large_delta_stable() {
  TEST_ASSERT_EQUAL_FLOAT(0.4f, approachHeat(0.4f, 0.9f, approachFactor(0, 500)));
  const float k = approachFactor(0xFFFFFFFFu, 500);
  TEST_ASSERT_TRUE(k >= 0.0f && k <= 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.9f, approachHeat(0.4f, 0.9f, k));
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

void test_spark_target_lands_in_the_spark_band() {
  const FireStyle twinkle = fireStyle(0);
  LoRng lo;
  HiRng hi;
  TEST_ASSERT_EQUAL_FLOAT(twinkle.sparkLo, rollSparkTarget(twinkle, lo));
  TEST_ASSERT_EQUAL_FLOAT(twinkle.sparkHi, rollSparkTarget(twinkle, hi));
}

// Twinkle at the burst paint threshold paints its anchor, so a spark crossing
// it fades in and out of the surface with no step. A saturated channel has no
// headroom to brighten into, so the anchors stay below full scale.
void test_burst_threshold_paints_the_anchor() {
  const FireStyle twinkle = fireStyle(0);
  const Color anchors[] = {Color(200, 120, 40, 0), Color(40, 80, 220, 30),
                           Color(180, 180, 180, 180)};
  for (const Color& a : anchors) {
    const Color p = shimmerPaint(a, kBurstPaintHeat, twinkle, 1.0f);
    TEST_ASSERT_INT_WITHIN(a.r / 25 + 2, a.r, p.r);
    TEST_ASSERT_INT_WITHIN(a.g / 25 + 2, a.g, p.g);
    TEST_ASSERT_INT_WITHIN(a.b / 25 + 2, a.b, p.b);
  }
}

// Steps a spark at the compositor's 16 ms cadence and returns its peak heat
// and its heat at the frame the burst completes.
static void runBurst(uint32_t burstMs, float target, float& peak, float& atEnd) {
  float heat = 0.0f;
  float t = target;
  peak = 0.0f;
  for (uint32_t ms = 16;; ms += 16) {
    if (burstFalling(ms, burstMs)) t = 0.0f;
    heat = approachHeat(heat, t, approachFactor(16, burstStepMs(burstMs)));
    peak = std::max(peak, heat);
    if (burstDone(ms, burstMs)) break;
  }
  atEnd = heat;
}

void test_burst_spark_rises_visible_and_ends_dark() {
  const FireStyle twinkle = fireStyle(0);
  for (uint32_t ms : {160u, 700u, 2000u, 2600u, 5000u, 10000u}) {
    float peak = 0.0f, atEnd = 1.0f;
    runBurst(ms, twinkle.sparkLo, peak, atEnd);
    TEST_ASSERT_TRUE_MESSAGE(peak > kBurstPaintHeat, "weakest spark never shows");
    TEST_ASSERT_TRUE_MESSAGE(atEnd < kBurstPaintHeat, "spark still lit at completion");
    runBurst(ms, twinkle.sparkHi, peak, atEnd);
    TEST_ASSERT_TRUE_MESSAGE(atEnd < kBurstPaintHeat, "hottest spark lit at completion");
  }
}

// A tinted star starts from the colour under it and reaches the tint's own
// ramp at the hottest spark.
void test_tinted_burst_fades_from_the_anchor_to_the_tint() {
  const FireStyle twinkle = fireStyle(0);
  const Color anchor(200, 40, 0, 0);
  const Color tint(0, 0, 220, 0);
  const Color cool = tintedBurstPaint(anchor, tint, kBurstPaintHeat, twinkle);
  TEST_ASSERT_TRUE(cool == anchor);
  const Color hot = tintedBurstPaint(anchor, tint, 1.0f, twinkle);
  TEST_ASSERT_TRUE(hot == shimmerPaint(tint, 1.0f, twinkle, 1.0f));
  const Color mid = tintedBurstPaint(anchor, tint, 0.7f, twinkle);
  TEST_ASSERT_TRUE(mid.b > 0 && mid.b < hot.b);
}

void test_burst_length_clamps() {
  TEST_ASSERT_EQUAL_UINT32(kBurstMinMs, clampBurstMs(1));
  TEST_ASSERT_EQUAL_UINT32(kBurstMaxMs, clampBurstMs(60000));
  TEST_ASSERT_EQUAL_UINT32(700, clampBurstMs(700));
  TEST_ASSERT_FALSE(burstFalling(349, 700));
  TEST_ASSERT_TRUE(burstFalling(350, 700));
  TEST_ASSERT_FALSE(burstDone(699, 700));
  TEST_ASSERT_TRUE(burstDone(700, 700));
  TEST_ASSERT_EQUAL_UINT32(5000, clampBurstMs(5000));
  TEST_ASSERT_EQUAL_UINT32(kBurstMaxMs, clampBurstMs(kBurstMaxMs + 1));
}

// Up to the swell a burst's shape scales with its length; past it the rise
// and fall keep the swell's pace and the fall starts one half-swell before
// the end, so the stars hold at peak in between.
void test_a_long_burst_keeps_the_swell_and_holds() {
  const uint32_t swellStep = burstStepMs(kBurstSwellMaxMs);
  for (uint32_t ms : {2600u, 5000u, 10000u}) {
    TEST_ASSERT_EQUAL_UINT32(swellStep, burstStepMs(ms));
    const uint32_t fallFrom = ms - kBurstSwellMaxMs / 2;
    TEST_ASSERT_FALSE(burstFalling(fallFrom - 1, ms));
    TEST_ASSERT_TRUE(burstFalling(fallFrom, ms));
  }
  TEST_ASSERT_TRUE(burstStepMs(1400) < swellStep);
  TEST_ASSERT_TRUE(burstFalling(700, 1400));
  TEST_ASSERT_TRUE(burstFalling(1000, 2000));
  TEST_ASSERT_FALSE(burstFalling(999, 2000));
}

// A held star is still near its target at the moment the fall begins.
void test_a_long_burst_holds_its_stars_at_peak() {
  const FireStyle twinkle = fireStyle(0);
  constexpr uint32_t kMs = 6000;
  float heat = 0.0f;
  uint32_t ms = 16;
  for (; !burstFalling(ms, kMs); ms += 16) {
    heat = approachHeat(heat, twinkle.sparkLo, approachFactor(16, burstStepMs(kMs)));
  }
  TEST_ASSERT_TRUE(ms > kMs / 2 + 1000);
  TEST_ASSERT_TRUE(heat > 0.99f * twinkle.sparkLo);
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
  RUN_TEST(test_flutter_is_frame_rate_independent);
  RUN_TEST(test_flutter_zero_delta_holds);
  RUN_TEST(test_flutter_large_delta_bounded);
  RUN_TEST(test_candle_flutter_travel_per_second_bounded);
  RUN_TEST(test_candle_heat_closes_at_most_4pct_per_frame);
  RUN_TEST(test_campfire_flutter_travel_per_second_bounded);
  RUN_TEST(test_campfire_heat_closes_at_most_6pct_per_frame);
  RUN_TEST(test_flutter_glides_between_steps);
  RUN_TEST(test_flutter_frame_moves_at_most_its_share_of_a_step);
  RUN_TEST(test_calm_styles_pinned);
  RUN_TEST(test_liveliness_order);
  RUN_TEST(test_approach_heat_moves_toward_target);
  RUN_TEST(test_approach_frame_rate_independent);
  RUN_TEST(test_approach_zero_and_large_delta_stable);
  RUN_TEST(test_heat_brightness_monotonic_with_floor);
  RUN_TEST(test_warmth_modulate_neutral_returns_anchor);
  RUN_TEST(test_warmth_modulate_direction_warm_anchor);
  RUN_TEST(test_warmth_modulate_channel_bounded_all_heat);
  RUN_TEST(test_warmth_modulate_zero_swing_is_pure_brightness);
  RUN_TEST(test_warmth_modulate_cool_anchor_valid);
  RUN_TEST(test_warmth_modulate_black_anchor_stays_black);
  RUN_TEST(test_spark_target_lands_in_the_spark_band);
  RUN_TEST(test_burst_threshold_paints_the_anchor);
  RUN_TEST(test_burst_spark_rises_visible_and_ends_dark);
  RUN_TEST(test_tinted_burst_fades_from_the_anchor_to_the_tint);
  RUN_TEST(test_burst_length_clamps);
  RUN_TEST(test_a_long_burst_keeps_the_swell_and_holds);
  RUN_TEST(test_a_long_burst_holds_its_stars_at_peak);
  return UNITY_END();
}
