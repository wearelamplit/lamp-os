// Pins JsonWriter, the heap-free serializer behind config saves and the BLE
// expressions section, byte-for-byte against ArduinoJson's serializeJson of
// the same documents, so a stored config and the wire stay unchanged.

#include <unity.h>

#include <ArduinoJson.h>

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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_stored_json_matches_arduinojson);
  RUN_TEST(test_default_stored_json_matches_arduinojson);
  RUN_TEST(test_expression_json_matches_arduinojson);
  RUN_TEST(test_short_buffer_reports_overflow);
  RUN_TEST(test_nested_arrays_and_empty_containers);
  RUN_TEST(test_write_exactly_keeps_only_an_exact_pass_and_the_last_good_otherwise);
  return UNITY_END();
}
