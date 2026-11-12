// settings_blob's `lamp` section over the real apply_lamp.hpp and a real
// Config. Brightness and BLE-name side effects are stubs.

#include <unity.h>

#include <ArduinoJson.h>

#include "components/apply/apply_lamp.hpp"

#include "../../src/config/config_codec.cpp"
#include "../../src/config/disposition_store.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"

lamp::Config config;

namespace lamp {
void updateAdvertisedDeviceName(const char*) {}
void clearBrightnessFadeSeed() {}
void applyEffectiveBrightness() {}
}  // namespace lamp

using namespace lamp;

namespace {
constexpr uint8_t kMaxBrightness = 255;

void writeLamp(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  apply::lampLocal(doc.as<JsonObject>(), kMaxBrightness);
}

void adopted(uint8_t apBootMinutes) {
  ::config.lamp = LampSettings{};
  ::config.lamp.setup = true;
  ::config.lamp.apBootMinutes = apBootMinutes;
}
}  // namespace

void setUp() { ::config.lamp = LampSettings{}; }
void tearDown() {}

void test_apbootminutes_applies_on_an_adopted_lamp() {
  for (const char* json : {"{\"apBootMinutes\":5}", "{\"apBootMinutes\":0}",
                           "{\"apBootMinutes\":255}", "{\"apBootMinutes\":17}"}) {
    adopted(2);
    JsonDocument doc;
    deserializeJson(doc, json);
    const int want = doc["apBootMinutes"].as<int>();
    apply::lampLocal(doc.as<JsonObject>(), kMaxBrightness);
    TEST_ASSERT_EQUAL_UINT8(want, ::config.lamp.apBootMinutes);
  }
}

void test_apbootminutes_out_of_range_keeps_the_stored_window() {
  for (const char* json : {"{\"apBootMinutes\":256}", "{\"apBootMinutes\":-1}",
                           "{\"apBootMinutes\":\"5\"}"}) {
    adopted(9);
    writeLamp(json);
    TEST_ASSERT_EQUAL_UINT8(9, ::config.lamp.apBootMinutes);
  }
}

void test_apbootminutes_absent_keeps_the_stored_window() {
  adopted(9);
  writeLamp("{\"name\":\"helena\"}");
  TEST_ASSERT_EQUAL_UINT8(9, ::config.lamp.apBootMinutes);
}

void test_adoption_stamps_a_window_unless_the_claim_carries_one() {
  writeLamp("{\"setup\":true}");
  TEST_ASSERT_EQUAL_UINT8(2, ::config.lamp.apBootMinutes);
  TEST_ASSERT_FALSE(::config.lamp.webappEnabled);

  ::config.lamp = LampSettings{};
  writeLamp("{\"setup\":true,\"apBootMinutes\":7,\"webappEnabled\":true}");
  TEST_ASSERT_EQUAL_UINT8(7, ::config.lamp.apBootMinutes);
  TEST_ASSERT_TRUE(::config.lamp.webappEnabled);
}

// Every app-writable lamp field lands on a lamp already adopted; none waits
// for the adoption edge.
void test_every_writable_field_applies_after_adoption() {
  adopted(2);
  const LampSettings before = ::config.lamp;
  writeLamp(
      "{\"name\":\"vamp\",\"password\":\"pw\",\"brightness\":37,\"advancedEnabled\":true,"
      "\"webappEnabled\":false,\"apBootMinutes\":11,\"socialMode\":2,"
      "\"brightnessCeiling\":90}");
  const LampSettings& now = ::config.lamp;
  TEST_ASSERT_EQUAL_STRING("vamp", now.name.c_str());
  TEST_ASSERT_EQUAL_STRING("pw", now.password.c_str());
  TEST_ASSERT_EQUAL_UINT8(37, now.brightness);
  TEST_ASSERT_NOT_EQUAL(before.advancedEnabled, now.advancedEnabled);
  TEST_ASSERT_NOT_EQUAL(before.webappEnabled, now.webappEnabled);
  TEST_ASSERT_EQUAL_UINT8(11, now.apBootMinutes);
  TEST_ASSERT_EQUAL(static_cast<int>(SocialMode::Extrovert), static_cast<int>(now.socialMode));
  TEST_ASSERT_EQUAL_UINT8(90, now.brightnessCeiling);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_apbootminutes_applies_on_an_adopted_lamp);
  RUN_TEST(test_apbootminutes_out_of_range_keeps_the_stored_window);
  RUN_TEST(test_apbootminutes_absent_keeps_the_stored_window);
  RUN_TEST(test_adoption_stamps_a_window_unless_the_claim_carries_one);
  RUN_TEST(test_every_writable_field_applies_after_adoption);
  return UNITY_END();
}
