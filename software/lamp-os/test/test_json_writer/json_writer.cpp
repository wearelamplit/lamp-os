// Pins JsonWriter, the heap-free serializer behind config saves and the BLE
// expressions section, byte-for-byte against ArduinoJson's serializeJson of
// the same documents, so a stored config and the wire stay unchanged.

#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "config/config_codec.hpp"
#include "util/json_writer.hpp"

#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

namespace {

struct Model {
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  HomeModeSettings homeMode;
};

std::string viaArduinoJson(const Model& m) {
  JsonDocument doc;
  config_codec::toStoredJson(doc.to<JsonObject>(), m.lamp, m.base, m.shade, m.homeMode);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string viaWriter(const Model& m) {
  JsonWriter measure(nullptr, 0);
  config_codec::writeStoredJson(measure, &m.lamp, m.base, m.shade, m.homeMode, nullptr);
  std::string out(measure.length() + 1, '\0');
  JsonWriter w(out.data(), out.size());
  config_codec::writeStoredJson(w, &m.lamp, m.base, m.shade, m.homeMode, nullptr);
  w.finish();
  TEST_ASSERT_TRUE(w.ok());
  TEST_ASSERT_EQUAL(measure.length(), w.length());
  out.resize(w.length());
  return out;
}

Model fullModel() {
  Model m;
  m.lamp.name = "jacko";
  m.lamp.password = "p\"a\\s\ns\tw\x01rd/";
  m.lamp.brightness = 255;
  m.lamp.setup = true;
  m.lamp.apBootMinutes = 7;
  m.base.byteOrder = "GRBW";
  m.base.segments = {{"Base \"main\"", 35, {Color(1, 2, 3, 4), Color(255, 254, 0, 9)}, 27},
                     {"Lions", 18, {Color(0x30, 0x07, 0x83, 0)}}};
  m.shade.segments = {{"Shade", 38, {Color(0xab, 0xcd, 0xef, 0x10)}}};
  m.homeMode.ssid = "caf\xc3\xa9 \b\f\r";
  m.homeMode.disabledExpressionTypes = {"glitchy", "pulse"};
  m.homeMode.enabled = true;
  return m;
}

}  // namespace

// The stored JSON a lamp that never touches rotation writes. Pinned
// literally so a field added to the stored shape cannot slip in at default.
constexpr const char* kDefaultStoredJson =
    "{\"lamp\":{\"name\":\"stray\",\"brightness\":100,\"setup\":false,\"named\":false,\"advance"
    "dEnabled\":false,\"webappEnabled\":true,\"apBootMinutes\":0,\"brightnessCeiling\":170,\"so"
    "cialMode\":1},\"base\":{\"colorsEditable\":true,\"segments\":[{\"name\":\"Base\",\"px\":0,"
    "\"colors\":[\"#30078300\"]}]},\"shade\":{\"colorsEditable\":true,\"segments\":[{\"name\":\""
    "Shade\",\"px\":0,\"colors\":[\"#000000ff\"]}]},\"homeMode\":{\"ssid\":\"\",\"brightness\":"
    "60,\"enabled\":false,\"networkBound\":false,\"socialDisabled\":true,\"disabledExpressionTy"
    "pes\":[\"glitchy\"]}}";

constexpr const char* kFullStoredJson =
    "{\"lamp\":{\"name\":\"jacko\",\"brightness\":255,\"password\":\"p\\\"a\\\\s\\ns\\tw\x01""r"
    "d/\",\"setup\":true,\"named\":false,\"advancedEnabled\":false,\"webappEnabled\":true,\"apB"
    "ootMinutes\":7,\"brightnessCeiling\":170,\"socialMode\":1},\"base\":{\"byteOrder\":\"GRBW\""
    ",\"colorsEditable\":true,\"segments\":[{\"name\":\"Base \\\"main\\\"\",\"px\":35,\"pin\":27,"
    "\"colors\":[\"#01020304\",\"#fffe0009\"]},{\"name\":\"Lions\",\"px\":18,\"colors\":[\"#30078300\"]}]"
    "},\"shade\":{\"colorsEditable\":true,\"segments\":[{\"name\":\"Shade\",\"px\":38,\"colors\""
    ":[\"#abcdef10\"]}]},\"homeMode\":{\"ssid\":\"caf\xc3""\xa9"" \\b\\f\\r\",\"brightness\":60"
    ",\"enabled\":true,\"networkBound\":false,\"socialDisabled\":true,\"disabledExpressionTypes"
    "\":[\"glitchy\",\"pulse\"]}}";

void test_default_stored_json_golden() {
  TEST_ASSERT_EQUAL_STRING(kDefaultStoredJson, viaWriter(Model{}).c_str());
}

void test_full_stored_json_golden() {
  TEST_ASSERT_EQUAL_STRING(kFullStoredJson, viaWriter(fullModel()).c_str());
}

void test_stored_json_matches_arduinojson() {
  const Model m = fullModel();
  TEST_ASSERT_EQUAL_STRING(viaArduinoJson(m).c_str(), viaWriter(m).c_str());
}

void test_default_stored_json_matches_arduinojson() {
  Model m;
  TEST_ASSERT_EQUAL_STRING(viaArduinoJson(m).c_str(), viaWriter(m).c_str());
}

void test_expression_json_matches_arduinojson() {
  ExpressionConfig e;
  e.type = "shifty";
  e.enabled = true;
  e.intervalMin = 0;
  e.intervalMax = 4294967295u;
  e.target = 3;
  e.colors = {Color(1, 2, 3, 4), Color(255, 255, 255, 255)};
  e.parameters = {{"fillMode", 2}, {"cascadeStaggerMs", 200}, {"opacity", 100}};
  JsonDocument doc;
  config_codec::expressionToJson(doc.to<JsonObject>(), e);
  std::string expected;
  serializeJson(doc, expected);
  char buf[512];
  JsonWriter w(buf, sizeof(buf));
  config_codec::writeExpressionJson(w, e);
  w.finish();
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), buf);
}

void test_short_buffer_reports_overflow() {
  const Model m = fullModel();
  char buf[32];
  JsonWriter w(buf, sizeof(buf));
  config_codec::writeStoredJson(w, &m.lamp, m.base, m.shade, m.homeMode, nullptr);
  w.finish();
  TEST_ASSERT_FALSE(w.ok());
  TEST_ASSERT_EQUAL(31, std::string(buf).size());
}

void test_nested_arrays_and_empty_containers() {
  char buf[64];
  JsonWriter w(buf, sizeof(buf));
  w.beginObject();
  w.key("a");
  w.beginArray();
  w.beginArray();
  w.endArray();
  w.beginObject();
  w.endObject();
  w.value(static_cast<uint32_t>(1));
  w.endArray();
  w.key("b");
  w.value(false);
  w.endObject();
  w.finish();
  TEST_ASSERT_EQUAL_STRING("{\"a\":[[],{},1],\"b\":false}", buf);
}

// The second pass must fill exactly what the first measured; anything else
// (the document changed in between) leaves nothing behind.
void test_write_exactly_keeps_only_an_exact_pass_and_the_last_good_otherwise() {
  std::string out = "stale";
  auto write = [](const char* text) {
    return [text](JsonWriter& w) {
      w.beginArray();
      w.value(text);
      w.endArray();
    };
  };
  TEST_ASSERT_TRUE(writeExactly(out, 7, write("abc")));
  TEST_ASSERT_EQUAL_STRING("[\"abc\"]", out.c_str());
  TEST_ASSERT_FALSE(writeExactly(out, 7, write("ab")));
  TEST_ASSERT_EQUAL_STRING("[\"abc\"]", out.c_str());
  TEST_ASSERT_FALSE(writeExactly(out, 7, write("abcd")));
  TEST_ASSERT_EQUAL_STRING("[\"abc\"]", out.c_str());
}

// Rotation on the one-segment shade is stored after its segments. The
// two-segment base never stores it, whatever it holds.
void test_rotation_written_identically_by_both_writers() {
  Model m = fullModel();
  m.shade.rotation = {true, 5, RotationDir::Down};
  m.base.rotation = {true, 9, RotationDir::Up};
  const std::string written = viaWriter(m);
  TEST_ASSERT_EQUAL_STRING(viaArduinoJson(m).c_str(), written.c_str());
  TEST_ASSERT_NOT_NULL(strstr(written.c_str(),
                              "\"shade\":{\"colorsEditable\":true,\"segments\":[{\"name\":\"Shade\","
                              "\"px\":38,\"colors\":[\"#abcdef10\"]}],\"rotate\":{\"on\":true,"
                              "\"periodS\":5,\"dir\":\"down\"}}"));
  TEST_ASSERT_EQUAL(written.find("\"rotate\""), written.rfind("\"rotate\""));
}

// The variant flag is never stored: a one-segment role writes the same JSON
// with it on and off, rotation set or at its default.
void test_rotation_flag_never_reaches_stored_json() {
  for (const ColorRotation& r :
       {ShadeSettings::kDefaultRotation, ColorRotation{true, 5, RotationDir::Up}}) {
    Model off = fullModel();
    off.shade.rotation = r;
    Model on = off;
    on.shade.rotationSupported = on.base.rotationSupported = true;
    TEST_ASSERT_EQUAL_STRING(viaWriter(off).c_str(), viaWriter(on).c_str());
    TEST_ASSERT_EQUAL_STRING(viaArduinoJson(off).c_str(), viaArduinoJson(on).c_str());
  }
  Model d;
  d.shade.rotationSupported = d.base.rotationSupported = true;
  TEST_ASSERT_EQUAL_STRING(kDefaultStoredJson, viaWriter(d).c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaultStoredJson, viaArduinoJson(d).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_stored_json_golden);
  RUN_TEST(test_full_stored_json_golden);
  RUN_TEST(test_stored_json_matches_arduinojson);
  RUN_TEST(test_default_stored_json_matches_arduinojson);
  RUN_TEST(test_expression_json_matches_arduinojson);
  RUN_TEST(test_short_buffer_reports_overflow);
  RUN_TEST(test_nested_arrays_and_empty_containers);
  RUN_TEST(test_write_exactly_keeps_only_an_exact_pass_and_the_last_good_otherwise);
  RUN_TEST(test_rotation_written_identically_by_both_writers);
  RUN_TEST(test_rotation_flag_never_reaches_stored_json);
  return UNITY_END();
}
