// CHAR_STATE_NOTIFY: the payload's quiet flag follows ota_quiet_mode, the
// predicate the write gates test, and tick() notifies on every quiet change,
// on connect and on subscribe.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include <cstdlib>

#include "components/firmware/ota_quiet_mode.cpp"
#include "components/network/ble/state_notify.cpp"
#include "util/heap_guard.cpp"
#include "../replaced_new/replaced_new.hpp"

static bool g_refuse = false;
void* testAllocate(size_t n) { return g_refuse ? nullptr : std::malloc(n); }
void testRelease(void* p) noexcept { std::free(p); }

using ble_control::buildStateNotifyJson;
using ble_control::StateNotifyFields;
using ble_control::StateNotifyTrigger;
using lamp::ota_quiet_mode::enterQuiet;
using lamp::ota_quiet_mode::exitQuiet;
using lamp::ota_quiet_mode::holders;

void setUp(void) {
  g_refuse = false;
  g_mock_largest_free_block = 1 << 20;
}
void tearDown(void) {
  g_refuse = false;
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

// The guarded build serves the same bytes; refuses, without running the fill,
// when the heap cannot spare it; and turns a failed allocation into a
// refusal, out empty, never a throw.
void test_guarded_build_matches_and_refuses_on_heap_failure() {
  using ble_control::buildStateNotifyGuarded;
  lamp::GreetingState greeting;
  greeting.active = true;
  greeting.peerLampId = "AA:BB:CC:DD:EE:FF";
  greeting.kind = "warm";
  StateNotifyFields plain;
  plain.greeting = &greeting;
  plain.nearbyRev = 7;
  std::string expected;
  buildStateNotifyJson(plain, expected);

  auto fill = [&](StateNotifyFields& f, lamp::GreetingState& g) {
    g = greeting;
    f.greeting = &g;
    f.nearbyRev = 7;
  };
  std::string out;
  TEST_ASSERT_TRUE(buildStateNotifyGuarded(fill, out));
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), out.c_str());

  bool filled = false;
  g_mock_largest_free_block = lamp::kHeapHeadroom;
  TEST_ASSERT_FALSE(buildStateNotifyGuarded(
      [&](StateNotifyFields&, lamp::GreetingState&) { filled = true; }, out));
  TEST_ASSERT_FALSE(filled);
  TEST_ASSERT_TRUE(out.empty());
  g_mock_largest_free_block = 1 << 20;

  std::string fresh;
  bool ok = true;
  g_refuse = true;
  try {
    ok = buildStateNotifyGuarded(fill, fresh);
  } catch (...) {
    g_refuse = false;
    TEST_FAIL_MESSAGE("buildStateNotifyGuarded threw");
  }
  g_refuse = false;
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_TRUE(fresh.empty());
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
  RUN_TEST(test_guarded_build_matches_and_refuses_on_heap_failure);
  return UNITY_END();
}
