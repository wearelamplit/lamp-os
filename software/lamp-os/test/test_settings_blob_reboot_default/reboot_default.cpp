// Native-host test: settings_blob WITHOUT a `reboot` key must default
// to reboot=true so old apps that don't know about the flag keep
// working as today.

#include <unity.h>

#include <ArduinoJson.h>

inline bool wantsReboot(JsonObject doc) {
  return doc["reboot"] | true;
}

void setUp(void) {}
void tearDown(void) {}

void test_missing_reboot_defaults_to_true() {
  JsonDocument doc;
  doc["lamp"]["name"] = "test";
  // No `reboot` key — old-app payload.
  TEST_ASSERT_TRUE(wantsReboot(doc.as<JsonObject>()));
}

void test_empty_object_defaults_to_true() {
  JsonDocument doc;
  doc.to<JsonObject>();
  TEST_ASSERT_TRUE(wantsReboot(doc.as<JsonObject>()));
}

void test_reboot_null_defaults_to_true() {
  // JsonVariant `null` treated as missing — `|` operator returns default.
  JsonDocument doc;
  doc["reboot"] = nullptr;
  TEST_ASSERT_TRUE(wantsReboot(doc.as<JsonObject>()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_missing_reboot_defaults_to_true);
  RUN_TEST(test_empty_object_defaults_to_true);
  RUN_TEST(test_reboot_null_defaults_to_true);
  return UNITY_END();
}
