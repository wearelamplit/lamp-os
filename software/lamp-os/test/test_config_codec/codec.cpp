// Native-host tests for config_codec — the JSON ↔ model parse/serialize that
// used to be inlined in Config's constructor and asJsonDocument. Covers field
// defaults, clamps, byte-order derivation, the home-mode migration, and a full
// serialize→parse round trip.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "config/config_codec.hpp"

// Native tests don't build src/, so compile the real implementation (and its
// color dependency) into this TU — same pattern as test_firmware_signature.
#include "../../src/util/color.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

namespace {
struct Model {
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;
  HomeModeSettings homeMode;
};

void parseInto(const char* json, Model& m) {
  JsonDocument doc;
  deserializeJson(doc, json);
  config_codec::fromJson(doc.as<JsonObject>(), m.lamp, m.base, m.shade,
                         m.expressions, m.homeMode);
}
}  // namespace

void test_empty_blob_uses_class_defaults() {
  Model m;
  parseInto("{}", m);
  TEST_ASSERT_EQUAL_STRING("stray", m.lamp.name.c_str());
  TEST_ASSERT_EQUAL_UINT8(100, m.lamp.brightness);
  TEST_ASSERT_EQUAL_UINT(1, m.base.colors.size());
  TEST_ASSERT_TRUE(m.base.colors[0] == kBaseDefaultColor);
  TEST_ASSERT_EQUAL_UINT(1, m.shade.colors.size());
  TEST_ASSERT_TRUE(m.shade.colors[0] == kShadeDefaultColor);
}

void test_socialmode_out_of_range_falls_back_to_ambivert() {
  Model m;
  parseInto("{\"lamp\":{\"socialMode\":9}}", m);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SocialMode::Ambivert),
                          static_cast<uint8_t>(m.lamp.socialMode));
}

void test_byteorder_derived_from_bpp_when_absent() {
  Model m;
  parseInto("{\"base\":{\"bpp\":3},\"shade\":{\"bpp\":4}}", m);
  TEST_ASSERT_EQUAL_STRING("GRB", m.base.byteOrder.c_str());
  TEST_ASSERT_EQUAL_STRING("GRBW", m.shade.byteOrder.c_str());
}

void test_homemode_enabled_migration() {
  Model withSsid;
  parseInto("{\"homeMode\":{\"ssid\":\"net\"}}", withSsid);
  TEST_ASSERT_TRUE(withSsid.homeMode.enabled);  // absent enabled + ssid → on

  Model noSsid;
  parseInto("{\"homeMode\":{\"ssid\":\"\"}}", noSsid);
  TEST_ASSERT_FALSE(noSsid.homeMode.enabled);
}

void test_round_trip_preserves_fields() {
  const char* json =
      "{\"lamp\":{\"name\":\"floor\",\"brightness\":42,\"socialMode\":2},"
      "\"base\":{\"px\":10,\"bpp\":3,\"colors\":[\"#11223300\"]},"
      "\"shade\":{\"px\":5,\"colors\":[\"#aabbcc00\"]},"
      "\"homeMode\":{\"ssid\":\"net\",\"brightness\":30,\"enabled\":true},"
      "\"expressions\":[{\"type\":\"twinkle\",\"enabled\":true,"
      "\"intervalMin\":5,\"intervalMax\":9,\"target\":2,\"speed\":3,"
      "\"colors\":[\"#ff000000\"]}]}";
  Model a;
  parseInto(json, a);

  JsonDocument out;
  config_codec::toJson(out.to<JsonObject>(), a.lamp, a.base, a.shade,
                       a.expressions, a.homeMode);
  std::string serialized;
  serializeJson(out, serialized);

  Model b;
  parseInto(serialized.c_str(), b);

  TEST_ASSERT_EQUAL_STRING("floor", b.lamp.name.c_str());
  TEST_ASSERT_EQUAL_UINT8(42, b.lamp.brightness);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SocialMode::Extrovert),
                          static_cast<uint8_t>(b.lamp.socialMode));
  TEST_ASSERT_EQUAL_UINT8(10, b.base.px);
  TEST_ASSERT_EQUAL_UINT8(3, b.base.bpp);
  TEST_ASSERT_EQUAL_STRING("GRB", b.base.byteOrder.c_str());
  TEST_ASSERT_TRUE(b.base.colors[0] == hexStringToColor("#11223300"));
  TEST_ASSERT_EQUAL_UINT8(5, b.shade.px);
  TEST_ASSERT_TRUE(b.shade.colors[0] == hexStringToColor("#aabbcc00"));
  TEST_ASSERT_EQUAL_STRING("net", b.homeMode.ssid.c_str());
  TEST_ASSERT_EQUAL_UINT8(30, b.homeMode.brightness);
  TEST_ASSERT_TRUE(b.homeMode.enabled);
  TEST_ASSERT_EQUAL_UINT(1, b.expressions.expressions.size());
  TEST_ASSERT_EQUAL_STRING("twinkle", b.expressions.expressions[0].type.c_str());
  TEST_ASSERT_TRUE(b.expressions.expressions[0].enabled);
  TEST_ASSERT_EQUAL_UINT(1, b.expressions.expressions[0].colors.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_blob_uses_class_defaults);
  RUN_TEST(test_socialmode_out_of_range_falls_back_to_ambivert);
  RUN_TEST(test_byteorder_derived_from_bpp_when_absent);
  RUN_TEST(test_homemode_enabled_migration);
  RUN_TEST(test_round_trip_preserves_fields);
  return UNITY_END();
}
