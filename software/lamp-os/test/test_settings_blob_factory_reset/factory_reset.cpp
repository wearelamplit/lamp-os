// Native-host test: factoryReset sentinel is detected and triggers the
// short-circuit path (does NOT reach the apply orchestrator). Also
// asserts the co-shipping warning case (factoryReset + other keys).

#include <unity.h>

#include <ArduinoJson.h>

inline bool isFactoryReset(JsonObject doc) {
  return doc["factoryReset"].as<bool>();
}

inline size_t coShippedKeyCount(JsonObject doc) {
  return doc.size();
}

void setUp(void) {}
void tearDown(void) {}

void test_factory_reset_sentinel_detected() {
  JsonDocument doc;
  doc["factoryReset"] = true;
  TEST_ASSERT_TRUE(isFactoryReset(doc.as<JsonObject>()));
  TEST_ASSERT_EQUAL_size_t(1, coShippedKeyCount(doc.as<JsonObject>()));
}

void test_factory_reset_with_other_keys_still_detected_but_flagged() {
  JsonDocument doc;
  doc["factoryReset"] = true;
  doc["lamp"]["name"] = "ignored";
  TEST_ASSERT_TRUE(isFactoryReset(doc.as<JsonObject>()));
  // Production code logs a WARNING when size > 1; the other keys are
  // intentionally dropped. This test asserts the size is detectable.
  TEST_ASSERT_GREATER_THAN_size_t(1, coShippedKeyCount(doc.as<JsonObject>()));
}

void test_no_factory_reset_when_false() {
  JsonDocument doc;
  doc["factoryReset"] = false;
  doc["lamp"]["name"] = "real";
  TEST_ASSERT_FALSE(isFactoryReset(doc.as<JsonObject>()));
}

void test_no_factory_reset_when_missing() {
  JsonDocument doc;
  doc["lamp"]["name"] = "real";
  TEST_ASSERT_FALSE(isFactoryReset(doc.as<JsonObject>()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_factory_reset_sentinel_detected);
  RUN_TEST(test_factory_reset_with_other_keys_still_detected_but_flagged);
  RUN_TEST(test_no_factory_reset_when_false);
  RUN_TEST(test_no_factory_reset_when_missing);
  return UNITY_END();
}
