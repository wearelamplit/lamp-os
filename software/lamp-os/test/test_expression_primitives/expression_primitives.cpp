#include <unity.h>
#include <array>
#include <map>
#include <string>

#include "expressions/param_utils.hpp"
#include "expressions/primitives.hpp"
#include "expressions/spotty/spotty_math.hpp"
#include "expressions/zone_preview.hpp"
#include "util/color.hpp"

using lamp::getParam;
using lamp::Zone;
using lamp::Points;
using lamp::parseSize;
using lamp::spotBlendPercent;
using lamp::edgeTaper;
using lamp::usableSections;
using lamp::randomPermutation;
using lamp::Color;
using lamp::buildZonePreviewBuffer;

static std::map<std::string, uint32_t> empty() { return {}; }

void test_getparam_absent_returns_fallback() {
  TEST_ASSERT_EQUAL_UINT32(7u, getParam(empty(), "nope", 7u));
}
void test_getparam_present_returns_value() {
  std::map<std::string, uint32_t> p = {{"k", 4u}};
  TEST_ASSERT_EQUAL_UINT32(4u, getParam(p, "k", 7u));
}

void test_zone_absent_spans_full_window() {
  Zone r = Zone::fromParameters(empty(), 144);
  TEST_ASSERT_EQUAL_UINT16(0, r.posMin);
  TEST_ASSERT_EQUAL_UINT16(143, r.posMax);
  TEST_ASSERT_EQUAL_UINT16(144, r.size());
}
void test_zone_clamps_to_window() {
  std::map<std::string, uint32_t> p = {{"posMin", 5}, {"posMax", 9999}};
  Zone r = Zone::fromParameters(p, 144);
  TEST_ASSERT_EQUAL_UINT16(5, r.posMin);
  TEST_ASSERT_EQUAL_UINT16(143, r.posMax);
}
void test_zone_swaps_reversed() {
  std::map<std::string, uint32_t> p = {{"posMin", 100}, {"posMax", 20}};
  Zone r = Zone::fromParameters(p, 144);
  TEST_ASSERT_EQUAL_UINT16(20, r.posMin);
  TEST_ASSERT_EQUAL_UINT16(100, r.posMax);
}
void test_zone_zero_window_is_empty() {
  Zone r = Zone::fromParameters(empty(), 0);
  TEST_ASSERT_EQUAL_UINT16(0, r.size());
}

void test_points_absent_returns_default() {
  Points pts = Points::fromParameters(empty(), 144, 1);
  TEST_ASSERT_EQUAL_UINT16(1, pts.count);
}
void test_points_clamps_to_window() {
  std::map<std::string, uint32_t> p = {{"count", 9999}};
  Points pts = Points::fromParameters(p, 10, 1);
  TEST_ASSERT_EQUAL_UINT16(10, pts.count);
}
void test_points_floor_is_one() {
  std::map<std::string, uint32_t> p = {{"count", 0}};
  Points pts = Points::fromParameters(p, 10, 3);
  TEST_ASSERT_EQUAL_UINT16(1, pts.count);
}

void test_parsesize_absent_returns_default() {
  TEST_ASSERT_EQUAL_UINT16(15, parseSize(empty(), 144, 15));
}
void test_parsesize_clamps_to_window() {
  std::map<std::string, uint32_t> p = {{"size", 9999}};
  TEST_ASSERT_EQUAL_UINT16(144, parseSize(p, 144, 15));
}
void test_parsesize_floor_is_one() {
  std::map<std::string, uint32_t> p = {{"size", 0}};
  TEST_ASSERT_EQUAL_UINT16(1, parseSize(p, 144, 15));
}

void test_resolve_zone_fullstrip_ignores_region() {
  std::map<std::string, uint32_t> p = {{"fullStrip", 1}, {"posMin", 5}, {"posMax", 20}};
  Zone r = lamp::resolveZone(p, 144);
  TEST_ASSERT_EQUAL_UINT16(0, r.posMin);
  TEST_ASSERT_EQUAL_UINT16(143, r.posMax);
}
void test_resolve_zone_region_honors_bounds() {
  std::map<std::string, uint32_t> p = {{"fullStrip", 0}, {"posMin", 5}, {"posMax", 20}};
  Zone r = lamp::resolveZone(p, 144);
  TEST_ASSERT_EQUAL_UINT16(5, r.posMin);
  TEST_ASSERT_EQUAL_UINT16(20, r.posMax);
}
void test_resolve_zone_defaults_to_fullstrip() {
  Zone r = lamp::resolveZone(empty(), 144);
  TEST_ASSERT_EQUAL_UINT16(0, r.posMin);
  TEST_ASSERT_EQUAL_UINT16(143, r.posMax);
}

void test_pulse_width_floor_is_three() {
  TEST_ASSERT_EQUAL_UINT16(3, lamp::pulseWidthFromPercent(5, 10));
}
void test_pulse_width_full_percent_is_half_of_zone() {
  // 100% over a 90px zone -> round(90 * 0.5) == 45 (0.5 == kPulseMaxWidthFrac).
  TEST_ASSERT_EQUAL_UINT16(45, lamp::pulseWidthFromPercent(100, 90));
}

void test_breathing_identity_defaults() {
  Zone r = Zone::fromParameters(empty(), 144);
  TEST_ASSERT_EQUAL_UINT16(0, r.posMin);
  TEST_ASSERT_EQUAL_UINT16(143, r.posMax);
}

void test_usable_sections_clamps_to_min_band_px() {
  TEST_ASSERT_EQUAL_UINT16(5, usableSections(5, 36));   // 36/5=7 fits, requested caps
  TEST_ASSERT_EQUAL_UINT16(2, usableSections(5, 12));   // 12/5=2
  TEST_ASSERT_EQUAL_UINT16(1, usableSections(5, 4));    // 4/5=0 -> floored to 1
}
void test_usable_sections_never_exceeds_requested() {
  TEST_ASSERT_EQUAL_UINT16(1, usableSections(1, 144));
  TEST_ASSERT_EQUAL_UINT16(3, usableSections(3, 144));
}
void test_usable_sections_floor_is_one() {
  TEST_ASSERT_EQUAL_UINT16(1, usableSections(0, 144));
}
void test_breathing_band_index_and_phase_offset() {
  const uint16_t zoneSize = 30;
  const uint16_t usable = usableSections(3, zoneSize);  // 30/5=6 fits -> 3
  TEST_ASSERT_EQUAL_UINT16(3, usable);
  auto band = [&](uint16_t off) {
    return static_cast<uint16_t>(static_cast<uint32_t>(off) * usable / zoneSize);
  };
  TEST_ASSERT_EQUAL_UINT16(0, band(0));
  TEST_ASSERT_EQUAL_UINT16(0, band(9));
  TEST_ASSERT_EQUAL_UINT16(1, band(10));
  TEST_ASSERT_EQUAL_UINT16(1, band(19));
  TEST_ASSERT_EQUAL_UINT16(2, band(20));
  TEST_ASSERT_EQUAL_UINT16(2, band(29));
  // phase offset = sectionOrder[band] * stagger; identity order here.
  const std::array<uint8_t, 5> order{{0, 1, 2, 0, 0}};
  const float stagger = 0.15f;
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.0f * stagger, order[band(0)] * stagger);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.0f * stagger, order[band(10)] * stagger);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.0f * stagger, order[band(20)] * stagger);
}

void test_breathing_sections_one_is_uniform() {
  const uint16_t zoneSize = 30;
  const uint16_t usable = usableSections(1, zoneSize);
  TEST_ASSERT_EQUAL_UINT16(1, usable);
  auto band = [&](uint16_t off) {
    return static_cast<uint16_t>(static_cast<uint32_t>(off) * usable / zoneSize);
  };
  TEST_ASSERT_EQUAL_UINT16(0, band(0));
  TEST_ASSERT_EQUAL_UINT16(0, band(7));
  TEST_ASSERT_EQUAL_UINT16(0, band(15));
  TEST_ASSERT_EQUAL_UINT16(0, band(29));
}
void test_breathing_last_pixel_band_no_overflow() {
  const uint16_t zoneSize = 36;
  const uint16_t usable = usableSections(5, zoneSize);  // 36/5=7 fits -> 5
  TEST_ASSERT_EQUAL_UINT16(5, usable);
  const uint16_t last =
      static_cast<uint16_t>(static_cast<uint32_t>(zoneSize - 1) * usable / zoneSize);
  TEST_ASSERT_EQUAL_UINT16(usable - 1, last);
}

void test_shifty_fillmode_default_is_uniform() {
  TEST_ASSERT_EQUAL_UINT32(0u, getParam(empty(), "fillMode", 0u));
}

void test_spotty_param_defaults() {
  TEST_ASSERT_EQUAL_UINT16(3, Points::fromParameters(empty(), 144, 3).count);
  TEST_ASSERT_EQUAL_UINT16(3, parseSize(empty(), 144, 3));
  Zone r = Zone::fromParameters(empty(), 144);
  TEST_ASSERT_EQUAL_UINT16(144, r.size());
  TEST_ASSERT_EQUAL_UINT32(3u, getParam(empty(), "spotSpeed", 3u));
}

void test_spot_blend_envelope_thirds() {
  const uint32_t life = 90;
  TEST_ASSERT_EQUAL_UINT32(0, spotBlendPercent(0, life));
  TEST_ASSERT_EQUAL_UINT32(50, spotBlendPercent(15, life));
  TEST_ASSERT_EQUAL_UINT32(100, spotBlendPercent(30, life));   // hold start
  TEST_ASSERT_EQUAL_UINT32(100, spotBlendPercent(59, life));   // hold end
  TEST_ASSERT_EQUAL_UINT32(50, spotBlendPercent(75, life));
  TEST_ASSERT_EQUAL_UINT32(100 / 30, spotBlendPercent(89, life));  // last frame
  TEST_ASSERT_EQUAL_UINT32(0, spotBlendPercent(90, life));   // cycle boundary
  TEST_ASSERT_EQUAL_UINT32(0, spotBlendPercent(500, life));  // past-life clamp
}

void test_spot_blend_envelope_easing_shapes_ramps() {
  const uint32_t life = 90;  // third = 30
  // Default (Linear) matches the plain ramp; endpoints stay fixed.
  TEST_ASSERT_EQUAL_UINT32(50, spotBlendPercent(15, life, lamp::Easing::Linear));
  TEST_ASSERT_EQUAL_UINT32(50, spotBlendPercent(75, life, lamp::Easing::Linear));

  // Swell (t^2) front-loads the fade-in and lingers longer on fade-out.
  TEST_ASSERT_EQUAL_UINT32(25, spotBlendPercent(15, life, lamp::Easing::Swell));  // 0.5^2 * 100
  TEST_ASSERT_EQUAL_UINT32(75, spotBlendPercent(75, life, lamp::Easing::Swell));  // (1-0.5^2) * 100

  // Hold and endpoints are curve-independent.
  TEST_ASSERT_EQUAL_UINT32(100, spotBlendPercent(45, life, lamp::Easing::Swell));
  TEST_ASSERT_EQUAL_UINT32(0, spotBlendPercent(0, life, lamp::Easing::Swell));
  TEST_ASSERT_EQUAL_UINT32(0, spotBlendPercent(90, life, lamp::Easing::Swell));
}

void test_spot_blend_envelope_degenerate_life() {
  // life < 3 has no thirds; full-on so a 1-2 ms spot is still visible.
  TEST_ASSERT_EQUAL_UINT32(100, spotBlendPercent(0, 1));
  TEST_ASSERT_EQUAL_UINT32(100, spotBlendPercent(1, 2));
}

void test_spot_life_bounds_band() {
  using lamp::spotLifeBounds;
  lamp::SpotLifeBounds fire = spotLifeBounds(1);
  TEST_ASSERT_EQUAL_UINT32(30, fire.lo);
  TEST_ASSERT_EQUAL_UINT32(2000, fire.hi);

  lamp::SpotLifeBounds stars = spotLifeBounds(10);
  TEST_ASSERT_EQUAL_UINT32(4000, stars.lo);
  TEST_ASSERT_EQUAL_UINT32(15000, stars.hi);

  uint32_t prevLo = 0, prevHi = 0;
  for (uint16_t s = 1; s <= 10; ++s) {
    lamp::SpotLifeBounds b = spotLifeBounds(s);
    TEST_ASSERT_TRUE(b.lo < b.hi);
    if (s > 1) {
      TEST_ASSERT_TRUE(b.lo > prevLo);
      TEST_ASSERT_TRUE(b.hi > prevHi);
    }
    prevLo = b.lo;
    prevHi = b.hi;
  }

  TEST_ASSERT_EQUAL_UINT32(30, spotLifeBounds(0).lo);
  TEST_ASSERT_EQUAL_UINT32(4000, spotLifeBounds(99).lo);
}

void test_edge_taper_linear_half_width_is_spotty_triangle() {
  using lamp::TaperCurve;

  TEST_ASSERT_EQUAL_UINT32(50, edgeTaper(0, 3, 3 / 2, TaperCurve::Linear));
  TEST_ASSERT_EQUAL_UINT32(100, edgeTaper(1, 3, 3 / 2, TaperCurve::Linear));
  TEST_ASSERT_EQUAL_UINT32(50, edgeTaper(2, 3, 3 / 2, TaperCurve::Linear));

  TEST_ASSERT_EQUAL_UINT32(33, edgeTaper(0, 5, 5 / 2, TaperCurve::Linear));
  TEST_ASSERT_EQUAL_UINT32(66, edgeTaper(1, 5, 5 / 2, TaperCurve::Linear));
  TEST_ASSERT_EQUAL_UINT32(100, edgeTaper(2, 5, 5 / 2, TaperCurve::Linear));
  TEST_ASSERT_EQUAL_UINT32(66, edgeTaper(3, 5, 5 / 2, TaperCurve::Linear));
  TEST_ASSERT_EQUAL_UINT32(33, edgeTaper(4, 5, 5 / 2, TaperCurve::Linear));
}

void test_edge_taper_quadratic_concentrates_at_edge() {
  using lamp::TaperCurve;
  const uint16_t size = 12, w = 5;

  // Interior (distFromEnd >= taperWidth) is flat 100.
  for (uint16_t i = w; i < size - w; ++i) {
    TEST_ASSERT_EQUAL_UINT32(100, edgeTaper(i, size, w, TaperCurve::Quadratic));
  }

  // Outer pixels rise monotonically toward the interior.
  uint32_t prev = 0;
  for (uint16_t d = 0; d <= w; ++d) {
    const uint32_t val = edgeTaper(d, size, w, TaperCurve::Quadratic);
    TEST_ASSERT_TRUE(val >= prev);
    prev = val;
  }

  // Ease-out: edge dimmer than d=2, which is dimmer than the interior.
  TEST_ASSERT_TRUE(edgeTaper(0, size, w, TaperCurve::Quadratic) <
                   edgeTaper(2, size, w, TaperCurve::Quadratic));
  TEST_ASSERT_TRUE(edgeTaper(2, size, w, TaperCurve::Quadratic) < 100);
}

void test_edge_taper_flat_interior() {
  using lamp::TaperCurve;
  TEST_ASSERT_EQUAL_UINT32(100, edgeTaper(5, 12, 5, TaperCurve::Quadratic));
  TEST_ASSERT_EQUAL_UINT32(100, edgeTaper(6, 12, 5, TaperCurve::Linear));
}

void test_edge_taper_size_one_is_full() {
  using lamp::TaperCurve;
  TEST_ASSERT_EQUAL_UINT32(100, edgeTaper(0, 1, 5, TaperCurve::Quadratic));
  TEST_ASSERT_EQUAL_UINT32(100, edgeTaper(0, 0, 5, TaperCurve::Linear));
}

void test_edge_taper_symmetric() {
  using lamp::TaperCurve;
  for (uint16_t i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_UINT32(edgeTaper(i, 6, 5, TaperCurve::Quadratic),
                             edgeTaper(6 - 1 - i, 6, 5, TaperCurve::Quadratic));
  }
  for (uint16_t i = 0; i < 12; ++i) {
    TEST_ASSERT_EQUAL_UINT32(edgeTaper(i, 12, 5, TaperCurve::Quadratic),
                             edgeTaper(12 - 1 - i, 12, 5, TaperCurve::Quadratic));
  }
}

void test_time_reached_plain() {
  TEST_ASSERT_FALSE(lamp::timeReached(5, 10));
  TEST_ASSERT_TRUE(lamp::timeReached(10, 10));
  TEST_ASSERT_TRUE(lamp::timeReached(11, 10));
}

void test_time_reached_across_wrap() {
  // Deadline armed just before the uint32 wrap; now has wrapped past it.
  TEST_ASSERT_FALSE(lamp::timeReached(0xFFFFFFFDu, 0xFFFFFFFEu));
  TEST_ASSERT_TRUE(lamp::timeReached(0x00000002u, 0xFFFFFFFEu));
  // A naive `now > deadline` would return false here.
  TEST_ASSERT_TRUE(lamp::timeReached(0x00000000u, 0xFFFFFFFFu));
}

void test_rewind_before_exhaust() {
  TEST_ASSERT_EQUAL_UINT32(50, lamp::rewindBeforeExhaust(50, 100));
  TEST_ASSERT_EQUAL_UINT32(98, lamp::rewindBeforeExhaust(98, 100));
  // Last frame before nextFrame() would flip STOPPED: rewound.
  TEST_ASSERT_EQUAL_UINT32(0, lamp::rewindBeforeExhaust(99, 100));
  TEST_ASSERT_EQUAL_UINT32(0, lamp::rewindBeforeExhaust(100, 100));
}

// The shifty hold-vs-frames race: a hold that outlives the frame budget must
// end on its millis deadline, never on the counter. Models the draw loop's
// rewind + nextFrame() exhaust check across many times the budget.
void test_frame_counter_cannot_end_a_hold() {
  const uint32_t frames = 100;
  uint32_t frame = 0;
  bool stopped = false;
  for (uint32_t tick = 0; tick < frames * 3; ++tick) {
    frame = lamp::rewindBeforeExhaust(frame, frames);
    frame += 1;
    if (frame >= frames) stopped = true;
  }
  TEST_ASSERT_FALSE(stopped);
}

void test_zone_preview_lights_span_rest_off() {
  const Color lit(0x11, 0x22, 0x33, 0x44);
  std::vector<Color> buf = buildZonePreviewBuffer(10, 3, 5, lit);
  TEST_ASSERT_EQUAL_UINT32(10, buf.size());
  for (uint16_t i = 0; i < 10; ++i) {
    if (i >= 3 && i <= 5) {
      TEST_ASSERT_TRUE(buf[i] == lit);
    } else {
      TEST_ASSERT_TRUE(buf[i] == Color());
    }
  }
}
void test_zone_preview_clamps_and_swaps() {
  const Color lit(0xFF, 0, 0, 0);
  std::vector<Color> buf = buildZonePreviewBuffer(10, 8, 999, lit);
  TEST_ASSERT_TRUE(buf[8] == lit);
  TEST_ASSERT_TRUE(buf[9] == lit);
  TEST_ASSERT_TRUE(buf[7] == Color());
  // reversed bounds: swap yields [3,5]
  std::vector<Color> swapped = buildZonePreviewBuffer(10, 5, 3, lit);
  for (uint16_t i = 0; i < 10; ++i) {
    if (i >= 3 && i <= 5) {
      TEST_ASSERT_TRUE(swapped[i] == lit);
    } else {
      TEST_ASSERT_TRUE(swapped[i] == Color());
    }
  }
}
void test_zone_preview_zero_pixels_empty() {
  std::vector<Color> buf = buildZonePreviewBuffer(0, 0, 5, Color(1, 1, 1, 1));
  TEST_ASSERT_EQUAL_UINT32(0, buf.size());
}

namespace {
// Counter rng: range(lo,hi) walks a fixed sequence mod the span, enough to
// exercise Fisher-Yates deterministically.
struct StubRng {
  uint32_t n = 0;
  uint32_t range(uint32_t lo, uint32_t hi) {
    if (hi <= lo) return lo;
    return lo + (n++ % (hi - lo + 1));
  }
};

bool isPermutation(const std::array<uint8_t, 5>& order, uint16_t n) {
  std::array<bool, 5> seen{};
  for (uint16_t i = 0; i < n; ++i) {
    if (order[i] >= n || seen[order[i]]) return false;
    seen[order[i]] = true;
  }
  return true;
}
}  // namespace

void test_random_permutation_is_valid() {
  for (uint16_t n : {2u, 3u, 5u}) {
    std::array<uint8_t, 5> order{};
    StubRng rng;
    randomPermutation(order, n, rng);
    TEST_ASSERT_TRUE(isPermutation(order, n));
  }
}

void test_random_permutation_single_is_identity() {
  std::array<uint8_t, 5> order{{9, 9, 9, 9, 9}};
  StubRng rng;
  randomPermutation(order, 1, rng);
  TEST_ASSERT_EQUAL_UINT8(0, order[0]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_getparam_absent_returns_fallback);
  RUN_TEST(test_getparam_present_returns_value);
  RUN_TEST(test_zone_absent_spans_full_window);
  RUN_TEST(test_zone_clamps_to_window);
  RUN_TEST(test_zone_swaps_reversed);
  RUN_TEST(test_zone_zero_window_is_empty);
  RUN_TEST(test_points_absent_returns_default);
  RUN_TEST(test_points_clamps_to_window);
  RUN_TEST(test_points_floor_is_one);
  RUN_TEST(test_parsesize_absent_returns_default);
  RUN_TEST(test_parsesize_clamps_to_window);
  RUN_TEST(test_parsesize_floor_is_one);
  RUN_TEST(test_resolve_zone_fullstrip_ignores_region);
  RUN_TEST(test_resolve_zone_region_honors_bounds);
  RUN_TEST(test_resolve_zone_defaults_to_fullstrip);
  RUN_TEST(test_pulse_width_floor_is_three);
  RUN_TEST(test_pulse_width_full_percent_is_half_of_zone);
  RUN_TEST(test_breathing_identity_defaults);
  RUN_TEST(test_usable_sections_clamps_to_min_band_px);
  RUN_TEST(test_usable_sections_never_exceeds_requested);
  RUN_TEST(test_usable_sections_floor_is_one);
  RUN_TEST(test_breathing_band_index_and_phase_offset);
  RUN_TEST(test_breathing_sections_one_is_uniform);
  RUN_TEST(test_breathing_last_pixel_band_no_overflow);
  RUN_TEST(test_random_permutation_is_valid);
  RUN_TEST(test_random_permutation_single_is_identity);
  RUN_TEST(test_shifty_fillmode_default_is_uniform);
  RUN_TEST(test_spotty_param_defaults);
  RUN_TEST(test_spot_blend_envelope_thirds);
  RUN_TEST(test_spot_blend_envelope_easing_shapes_ramps);
  RUN_TEST(test_spot_blend_envelope_degenerate_life);
  RUN_TEST(test_spot_life_bounds_band);
  RUN_TEST(test_edge_taper_linear_half_width_is_spotty_triangle);
  RUN_TEST(test_edge_taper_quadratic_concentrates_at_edge);
  RUN_TEST(test_edge_taper_flat_interior);
  RUN_TEST(test_edge_taper_size_one_is_full);
  RUN_TEST(test_edge_taper_symmetric);
  RUN_TEST(test_time_reached_plain);
  RUN_TEST(test_time_reached_across_wrap);
  RUN_TEST(test_rewind_before_exhaust);
  RUN_TEST(test_frame_counter_cannot_end_a_hold);
  RUN_TEST(test_zone_preview_lights_span_rest_off);
  RUN_TEST(test_zone_preview_clamps_and_swaps);
  RUN_TEST(test_zone_preview_zero_pixels_empty);
  return UNITY_END();
}
