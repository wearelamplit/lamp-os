// Native-host test for apply::pixelFormatLocal — the settings_blob path
// that applies a surface's px / bpp / byteOrder. Regression guard for the
// bug where the app wrote shade.px (and base.px/bpp/byteOrder) but the
// firmware never applied them, so the lamp rebooted with the old strip.
//
// pixelFormatLocal is a template, so this exercises the REAL production
// function body. The surface is a local stand-in with the same fields as
// Base/ShadeSettings — the native env can't link Color's out-of-line ctor
// (color.cpp), so the full structs can't be constructed here. Same
// mirror-class discipline as the other apply tests.

#include <unity.h>

#include <cstdint>
#include <string>

#include <ArduinoJson.h>

#include "components/apply/apply_pixel_format.hpp"

namespace lamp {
// Mirrors the px/bpp/byteOrder fields of Base/ShadeSettings (config_types.hpp).
struct Surface {
  uint8_t px = 32;
  uint8_t bpp = 4;
  std::string byteOrder = "";
};
using ShadeSettings = Surface;  // class default px = 32 (set per test)
using BaseSettings = Surface;   // class default px = 36 (set per test)
}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

static JsonDocument blob(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  return doc;
}

void test_shade_px_applied_from_blob() {
  lamp::ShadeSettings shade;  // class default px = 32
  JsonDocument doc = blob("{\"px\":40}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), shade);
  TEST_ASSERT_EQUAL_UINT8(40, shade.px);
}

void test_base_px_applied_from_blob() {
  lamp::BaseSettings base;  // class default px = 36
  JsonDocument doc = blob("{\"px\":35}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), base);
  TEST_ASSERT_EQUAL_UINT8(35, base.px);
}

void test_absent_px_leaves_current() {
  lamp::ShadeSettings shade;
  JsonDocument doc = blob("{\"bpp\":3}");  // no px key
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), shade);
  TEST_ASSERT_EQUAL_UINT8(32, shade.px);
}

void test_px_clamped_to_50() {
  lamp::ShadeSettings shade;
  JsonDocument doc = blob("{\"px\":99}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), shade);
  TEST_ASSERT_EQUAL_UINT8(50, shade.px);
}

void test_px_zero_ignored() {
  lamp::ShadeSettings shade;  // 32
  JsonDocument doc = blob("{\"px\":0}");  // invalid — a strip has >=1 pixel
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), shade);
  TEST_ASSERT_EQUAL_UINT8(32, shade.px);
}

void test_bpp_applied_when_valid() {
  lamp::ShadeSettings shade;  // 4
  JsonDocument doc = blob("{\"bpp\":3}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), shade);
  TEST_ASSERT_EQUAL_UINT8(3, shade.bpp);
}

void test_bpp_ignored_when_invalid() {
  lamp::ShadeSettings shade;  // 4
  JsonDocument doc = blob("{\"bpp\":7}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), shade);
  TEST_ASSERT_EQUAL_UINT8(4, shade.bpp);
}

void test_byte_order_applied() {
  lamp::ShadeSettings shade;  // ""
  JsonDocument doc = blob("{\"byteOrder\":\"BGR\"}");
  lamp::apply::pixelFormatLocal(doc.as<JsonObjectConst>(), shade);
  TEST_ASSERT_EQUAL_STRING("BGR", shade.byteOrder.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_shade_px_applied_from_blob);
  RUN_TEST(test_base_px_applied_from_blob);
  RUN_TEST(test_absent_px_leaves_current);
  RUN_TEST(test_px_clamped_to_50);
  RUN_TEST(test_px_zero_ignored);
  RUN_TEST(test_bpp_applied_when_valid);
  RUN_TEST(test_bpp_ignored_when_invalid);
  RUN_TEST(test_byte_order_applied);
  return UNITY_END();
}
