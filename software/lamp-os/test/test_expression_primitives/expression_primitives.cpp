#include <unity.h>
#include <map>
#include <string>

#include "expressions/param_utils.hpp"
#include "expressions/primitives.hpp"
#include "expressions/zone_preview.hpp"
#include "util/color.hpp"

using lamp::getParam;
using lamp::Zone;
using lamp::Points;
using lamp::parseSize;
using lamp::Scatter;
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

void test_parsesize_default_matches_pulse_width() {
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

void test_scatter_absent_is_zero() {
  TEST_ASSERT_EQUAL_UINT8(0, Scatter::fromParameters(empty()).percent);
}
void test_scatter_clamps_to_100() {
  std::map<std::string, uint32_t> p = {{"scatter", 250}};
  TEST_ASSERT_EQUAL_UINT8(100, Scatter::fromParameters(p).percent);
}

void test_breathing_identity_defaults() {
  Zone r = Zone::fromParameters(empty(), 144);
  TEST_ASSERT_EQUAL_UINT16(0, r.posMin);
  TEST_ASSERT_EQUAL_UINT16(143, r.posMax);
  TEST_ASSERT_EQUAL_UINT8(0, Scatter::fromParameters(empty()).percent);
}

void test_shifty_fillmode_default_is_uniform() {
  TEST_ASSERT_EQUAL_UINT32(0u, getParam(empty(), "fillMode", 0u));
}

void test_spotty_param_defaults() {
  TEST_ASSERT_EQUAL_UINT16(3, Points::fromParameters(empty(), 144, 3).count);
  TEST_ASSERT_EQUAL_UINT16(4, parseSize(empty(), 144, 4));
  Zone r = Zone::fromParameters(empty(), 144);
  TEST_ASSERT_EQUAL_UINT16(144, r.size());
  TEST_ASSERT_EQUAL_UINT32(3u, getParam(empty(), "spotSpeed", 3u));
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
  RUN_TEST(test_parsesize_default_matches_pulse_width);
  RUN_TEST(test_parsesize_clamps_to_window);
  RUN_TEST(test_parsesize_floor_is_one);
  RUN_TEST(test_scatter_absent_is_zero);
  RUN_TEST(test_scatter_clamps_to_100);
  RUN_TEST(test_breathing_identity_defaults);
  RUN_TEST(test_shifty_fillmode_default_is_uniform);
  RUN_TEST(test_spotty_param_defaults);
  RUN_TEST(test_zone_preview_lights_span_rest_off);
  RUN_TEST(test_zone_preview_clamps_and_swaps);
  RUN_TEST(test_zone_preview_zero_pixels_empty);
  return UNITY_END();
}
