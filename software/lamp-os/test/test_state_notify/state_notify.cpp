// CHAR_STATE_NOTIFY: the payload's quiet flag follows ota_quiet_mode, the
// predicate the write gates test, and tick() notifies on every quiet change,
// on connect and on subscribe.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "components/firmware/ota_quiet_mode.cpp"
#include "components/network/ble/state_notify.cpp"

using ble_control::buildStateNotifyJson;
using ble_control::StateNotifyFields;
using ble_control::StateNotifyTrigger;
using lamp::ota_quiet_mode::enterQuiet;
using lamp::ota_quiet_mode::exitQuiet;
using lamp::ota_quiet_mode::holders;

void setUp(void) {}
void tearDown(void) {
  while (holders() > 0) exitQuiet();
}

static JsonDocument parse(const StateNotifyFields& f) {
  std::string out;
  buildStateNotifyJson(f, out);
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(deserializeJson(doc, out) == DeserializationError::Ok,
                           out.c_str());
  return doc;
}

void test_quiet_follows_quiet_mode() {
  StateNotifyFields f;
  JsonDocument doc = parse(f);
  TEST_ASSERT_TRUE(doc["quiet"].is<bool>());
  TEST_ASSERT_FALSE(doc["quiet"].as<bool>());

  enterQuiet(false, true);
  TEST_ASSERT_TRUE(parse(f)["quiet"].as<bool>());
  enterQuiet(false, true);
  exitQuiet();
  TEST_ASSERT_TRUE_MESSAGE(parse(f)["quiet"].as<bool>(), "one holder left");
  exitQuiet();
  TEST_ASSERT_FALSE(parse(f)["quiet"].as<bool>());
}

void test_other_fields_survive_beside_quiet() {
  lamp::GreetingState g;
  g.active     = true;
  g.peerLampId = "AA:BB:CC:DD:EE:0\"1";
  g.kind       = "warm";
  StateNotifyFields f;
  f.previewActive = true;
  f.greeting      = &g;
  f.nearbyRev     = 4000000000u;
  enterQuiet(false, true);
  JsonDocument doc = parse(f);
  TEST_ASSERT_TRUE(doc["previewActive"].as<bool>());
  TEST_ASSERT_TRUE(doc["greeting"]["active"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:0\"1",
                           doc["greeting"]["peer"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("warm", doc["greeting"]["kind"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(4000000000u, doc["nearbyRev"].as<uint32_t>());
  TEST_ASSERT_TRUE(doc["quiet"].as<bool>());
}

void test_greeting_absent_without_a_source() {
  StateNotifyFields f;
  JsonDocument doc = parse(f);
  TEST_ASSERT_FALSE(doc["greeting"].is<JsonObject>());
  lamp::GreetingState idle;
  f.greeting = &idle;
  doc = parse(f);
  TEST_ASSERT_FALSE(doc["greeting"]["active"].as<bool>());
}

void test_trigger_fires_on_each_quiet_change_while_connected() {
  StateNotifyTrigger t;
  TEST_ASSERT_TRUE(t.step(true, false));   // connect
  TEST_ASSERT_FALSE(t.step(true, false));
  enterQuiet(false, true);                 // push enters quiet
  TEST_ASSERT_TRUE(t.step(true, false));
  TEST_ASSERT_FALSE(t.step(true, false));
  enterQuiet(false, true);                 // a second holder is no change
  TEST_ASSERT_FALSE(t.step(true, false));
  exitQuiet();
  TEST_ASSERT_FALSE(t.step(true, false));
  exitQuiet();                             // quiet ends
  TEST_ASSERT_TRUE(t.step(true, false));
  TEST_ASSERT_FALSE(t.step(true, false));
}

void test_trigger_fires_on_subscribe() {
  StateNotifyTrigger t;
  enterQuiet(false, true);
  t.step(true, false);
  TEST_ASSERT_FALSE(t.step(true, false));
  TEST_ASSERT_TRUE(t.step(true, true));
  TEST_ASSERT_FALSE(t.step(true, false));
}

void test_trigger_is_silent_without_a_client() {
  StateNotifyTrigger t;
  enterQuiet(false, true);
  TEST_ASSERT_FALSE(t.step(false, false));
  TEST_ASSERT_FALSE(t.step(false, true));
  exitQuiet();
  TEST_ASSERT_FALSE(t.step(false, false));
  // A quiet change while nobody is connected is carried by the next connect.
  TEST_ASSERT_TRUE(t.step(true, false));
  TEST_ASSERT_FALSE(t.step(true, false));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_quiet_follows_quiet_mode);
  RUN_TEST(test_other_fields_survive_beside_quiet);
  RUN_TEST(test_greeting_absent_without_a_source);
  RUN_TEST(test_trigger_fires_on_each_quiet_change_while_connected);
  RUN_TEST(test_trigger_fires_on_subscribe);
  RUN_TEST(test_trigger_is_silent_without_a_client);
  return UNITY_END();
}
