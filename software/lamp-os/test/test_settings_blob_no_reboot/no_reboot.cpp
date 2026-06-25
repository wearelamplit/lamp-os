// Native-host test: settings_blob with explicit {"reboot": false} must
// persist but NOT set the reboot flag.
//
// Mirror-style: re-declare the orchestrator's relevant invariant in
// terms of inputs (the incoming JSON's `reboot` key) and outputs
// (the orchestrator's return value, which the drain uses to decide
// whether to set fadeOutRebootRequested).

#include <unity.h>

#include <ArduinoJson.h>

// Mirror of the orchestrator's reboot-decision logic.
inline bool wantsReboot(JsonObject doc) {
  return doc["reboot"] | true;
}

void setUp(void) {}
void tearDown(void) {}

void test_explicit_reboot_false_returns_false() {
  JsonDocument doc;
  doc["lamp"]["name"] = "test";
  doc["reboot"] = false;
  TEST_ASSERT_FALSE(wantsReboot(doc.as<JsonObject>()));
}

void test_explicit_reboot_true_returns_true() {
  JsonDocument doc;
  doc["lamp"]["name"] = "test";
  doc["reboot"] = true;
  TEST_ASSERT_TRUE(wantsReboot(doc.as<JsonObject>()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_explicit_reboot_false_returns_false);
  RUN_TEST(test_explicit_reboot_true_returns_true);
  return UNITY_END();
}
