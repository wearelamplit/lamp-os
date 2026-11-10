// CHAR_STATE_NOTIFY: the payload carries the quiet flag the app waits on, and
// tick() notifies on every quiet change, on connect and on subscribe.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "components/network/ble/state_notify.cpp"

using ble_control::buildStateNotifyJson;
using ble_control::StateNotifyFields;
using ble_control::StateNotifyTrigger;

void setUp(void) {}
void tearDown(void) {}

static JsonDocument parse(const StateNotifyFields& f) {
  std::string out;
  buildStateNotifyJson(f, out);
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(deserializeJson(doc, out) == DeserializationError::Ok,
                           out.c_str());
  return doc;
}

void test_quiet_follows_the_input() {
  for (const bool quiet : {false, true}) {
    StateNotifyFields f;
    f.quiet = quiet;
    JsonDocument doc = parse(f);
    TEST_ASSERT_TRUE(doc["quiet"].is<bool>());
    TEST_ASSERT_EQUAL(quiet, doc["quiet"].as<bool>());
  }
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
  f.quiet         = true;
  JsonDocument doc = parse(f);
  TEST_ASSERT_TRUE(doc["previewActive"].as<bool>());
  TEST_ASSERT_TRUE(doc["greeting"]["active"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:0\"1",
                           doc["greeting"]["peer"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("warm", doc["greeting"]["kind"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(4000000000u, doc["nearbyRev"].as<uint32_t>());
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
  TEST_ASSERT_TRUE(t.step(true, false, false));   // connect
  TEST_ASSERT_FALSE(t.step(true, false, false));
  TEST_ASSERT_TRUE(t.step(true, false, true));    // push enters quiet
  TEST_ASSERT_FALSE(t.step(true, false, true));
  TEST_ASSERT_FALSE(t.step(true, false, true));
  TEST_ASSERT_TRUE(t.step(true, false, false));   // quiet ends
  TEST_ASSERT_FALSE(t.step(true, false, false));
}

void test_trigger_fires_on_subscribe() {
  StateNotifyTrigger t;
  t.step(true, false, true);
  TEST_ASSERT_FALSE(t.step(true, false, true));
  TEST_ASSERT_TRUE(t.step(true, true, true));
  TEST_ASSERT_FALSE(t.step(true, false, true));
}

void test_trigger_is_silent_without_a_client() {
  StateNotifyTrigger t;
  TEST_ASSERT_FALSE(t.step(false, false, true));
  TEST_ASSERT_FALSE(t.step(false, true, false));
  // A quiet change while nobody is connected is carried by the next connect.
  TEST_ASSERT_TRUE(t.step(true, false, true));
  TEST_ASSERT_FALSE(t.step(true, false, true));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_quiet_follows_the_input);
  RUN_TEST(test_other_fields_survive_beside_quiet);
  RUN_TEST(test_greeting_absent_without_a_source);
  RUN_TEST(test_trigger_fires_on_each_quiet_change_while_connected);
  RUN_TEST(test_trigger_fires_on_subscribe);
  RUN_TEST(test_trigger_is_silent_without_a_client);
  return UNITY_END();
}
