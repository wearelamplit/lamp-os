// Native-host test for apply::pixelFormatLocal — the settings_blob path that
// applies per-segment pixel counts. Regression guard for the bug where the
// app wrote a segment px but the firmware never applied it, so the lamp
// rebooted with the old strip.
//
// pixelFormatLocal is a template, so this exercises the REAL production
// function body. The surface is a local stand-in with the same `segments`
// shape as Base/ShadeSettings — the native env can't link Color's out-of-line
// ctor (color.cpp), so the segment mirror carries only px. Same mirror-class
// discipline as the other apply tests.

#include <unity.h>

#include <cstdint>
#include <vector>

#include <ArduinoJson.h>

#include "components/apply/apply_pixel_format.hpp"

namespace lamp {
struct Seg {
  uint8_t px = 0;
};
struct Surface {
  std::vector<Seg> segments;
};
}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

static JsonDocument blob(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  return doc;
}

static lamp::Surface twoSeg(uint8_t a, uint8_t b) {
  lamp::Surface s;
  s.segments = {{a}, {b}};
  return s;
}

void test_segment_px_applied_from_blob() {
  lamp::Surface s = twoSeg(16, 12);
  JsonDocument doc = blob("{\"segments\":[{\"px\":20},{\"px\":9}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT8(20, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(9, s.segments[1].px);
}

void test_absent_segments_leaves_current() {
  lamp::Surface s = twoSeg(16, 12);
  JsonDocument doc = blob("{\"colors\":[]}");  // no segments key
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT8(16, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(12, s.segments[1].px);
}

void test_absent_px_on_segment_leaves_current() {
  lamp::Surface s = twoSeg(16, 12);
  JsonDocument doc = blob("{\"segments\":[{\"name\":\"x\"},{\"px\":9}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT8(16, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(9, s.segments[1].px);
}

void test_px_zero_ignored() {
  lamp::Surface s = twoSeg(16, 12);
  JsonDocument doc = blob("{\"segments\":[{\"px\":0},{\"px\":0}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT8(16, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(12, s.segments[1].px);
}

void test_extra_blob_segments_ignored() {
  lamp::Surface s = twoSeg(16, 12);
  JsonDocument doc =
      blob("{\"segments\":[{\"px\":20},{\"px\":9},{\"px\":30}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT(2, s.segments.size());
  TEST_ASSERT_EQUAL_UINT8(20, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(9, s.segments[1].px);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_segment_px_applied_from_blob);
  RUN_TEST(test_absent_segments_leaves_current);
  RUN_TEST(test_absent_px_on_segment_leaves_current);
  RUN_TEST(test_px_zero_ignored);
  RUN_TEST(test_extra_blob_segments_ignored);
  return UNITY_END();
}
