// Native-host test for apply::pixelFormatLocal — the settings_blob path that
// applies a role's strip format (per-segment pixel counts + role-level
// byteOrder). Regression guard for the bug where the app wrote a segment px
// or byteOrder but the firmware never applied it, so the lamp rebooted with
// the old strip. Also pins byteOrderFromString (the string→enum mapping the
// strip build resolves at boot).
//
// pixelFormatLocal is a template, so this exercises the REAL production
// function body. The surface is a local stand-in with the same `segments` +
// `byteOrder` shape as Base/ShadeSettings — the native env can't link Color's
// out-of-line ctor (color.cpp), so the segment mirror carries only px. Same
// mirror-class discipline as the other apply tests.

#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "components/apply/apply_pixel_format.hpp"
#include "core/hw_config.hpp"

namespace lamp {
struct Seg {
  uint8_t px = 0;
};
struct SurfaceMirror {
  std::vector<Seg> segments;
  std::string byteOrder = "";
};
}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

static JsonDocument blob(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  return doc;
}

static lamp::SurfaceMirror twoSeg(uint8_t a, uint8_t b) {
  lamp::SurfaceMirror s;
  s.segments = {{a}, {b}};
  return s;
}

void test_segment_px_applied_from_blob() {
  lamp::SurfaceMirror s = twoSeg(16, 12);
  JsonDocument doc = blob("{\"segments\":[{\"px\":20},{\"px\":9}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT8(20, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(9, s.segments[1].px);
}

void test_absent_segments_leaves_current() {
  lamp::SurfaceMirror s = twoSeg(16, 12);
  JsonDocument doc = blob("{\"colors\":[]}");  // no segments key
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT8(16, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(12, s.segments[1].px);
}

void test_absent_px_on_segment_leaves_current() {
  lamp::SurfaceMirror s = twoSeg(16, 12);
  JsonDocument doc = blob("{\"segments\":[{\"name\":\"x\"},{\"px\":9}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT8(16, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(9, s.segments[1].px);
}

void test_px_zero_ignored() {
  lamp::SurfaceMirror s = twoSeg(16, 12);
  JsonDocument doc = blob("{\"segments\":[{\"px\":0},{\"px\":0}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT8(16, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(12, s.segments[1].px);
}

void test_extra_blob_segments_ignored() {
  lamp::SurfaceMirror s = twoSeg(16, 12);
  JsonDocument doc =
      blob("{\"segments\":[{\"px\":20},{\"px\":9},{\"px\":30}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_UINT(2, s.segments.size());
  TEST_ASSERT_EQUAL_UINT8(20, s.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(9, s.segments[1].px);
}

void test_byte_order_applied_from_blob() {
  lamp::SurfaceMirror s = twoSeg(16, 12);  // byteOrder ""
  JsonDocument doc = blob("{\"byteOrder\":\"BGR\"}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_STRING("BGR", s.byteOrder.c_str());
}

void test_byte_order_applied_alongside_segments() {
  lamp::SurfaceMirror s = twoSeg(16, 12);
  JsonDocument doc = blob("{\"byteOrder\":\"GRB\",\"segments\":[{\"px\":20}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_STRING("GRB", s.byteOrder.c_str());
  TEST_ASSERT_EQUAL_UINT8(20, s.segments[0].px);
}

void test_byte_order_absent_leaves_current() {
  lamp::SurfaceMirror s = twoSeg(16, 12);
  s.byteOrder = "GRBW";
  JsonDocument doc = blob("{\"segments\":[{\"px\":20}]}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), s);
  TEST_ASSERT_EQUAL_STRING("GRBW", s.byteOrder.c_str());
}

void test_byte_order_string_maps_to_enum() {
  lamp::ByteOrder order = lamp::ByteOrder::GRBW;
  TEST_ASSERT_TRUE(lamp::byteOrderFromString("BGR", order));
  TEST_ASSERT_EQUAL(static_cast<int>(lamp::ByteOrder::BGR),
                    static_cast<int>(order));
  TEST_ASSERT_TRUE(lamp::byteOrderFromString("GRB", order));
  TEST_ASSERT_EQUAL(static_cast<int>(lamp::ByteOrder::GRB),
                    static_cast<int>(order));
  TEST_ASSERT_TRUE(lamp::byteOrderFromString("GRBW", order));
  TEST_ASSERT_EQUAL(static_cast<int>(lamp::ByteOrder::GRBW),
                    static_cast<int>(order));
}

void test_byte_order_empty_falls_back_to_default() {
  lamp::ByteOrder order = lamp::ByteOrder::BGR;  // strip's compile-time default
  TEST_ASSERT_FALSE(lamp::byteOrderFromString("", order));
  TEST_ASSERT_EQUAL(static_cast<int>(lamp::ByteOrder::BGR),
                    static_cast<int>(order));
}

void test_byte_order_unrecognized_falls_back_to_default() {
  lamp::ByteOrder order = lamp::ByteOrder::GRB;
  TEST_ASSERT_FALSE(lamp::byteOrderFromString("RGBW", order));
  TEST_ASSERT_EQUAL(static_cast<int>(lamp::ByteOrder::GRB),
                    static_cast<int>(order));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_segment_px_applied_from_blob);
  RUN_TEST(test_absent_segments_leaves_current);
  RUN_TEST(test_absent_px_on_segment_leaves_current);
  RUN_TEST(test_px_zero_ignored);
  RUN_TEST(test_extra_blob_segments_ignored);
  RUN_TEST(test_byte_order_applied_from_blob);
  RUN_TEST(test_byte_order_applied_alongside_segments);
  RUN_TEST(test_byte_order_absent_leaves_current);
  RUN_TEST(test_byte_order_string_maps_to_enum);
  RUN_TEST(test_byte_order_empty_falls_back_to_default);
  RUN_TEST(test_byte_order_unrecognized_falls_back_to_default);
  return UNITY_END();
}
