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
  TEST_ASSERT_TRUE(t.step(true, false, 0));   // connect
  TEST_ASSERT_FALSE(t.step(true, false, 0));
  enterQuiet(false, true);                 // push enters quiet
  TEST_ASSERT_TRUE(t.step(true, false, 0));
  TEST_ASSERT_FALSE(t.step(true, false, 0));
  enterQuiet(false, true);                 // a second holder is no change
  TEST_ASSERT_FALSE(t.step(true, false, 0));
  exitQuiet();
  TEST_ASSERT_FALSE(t.step(true, false, 0));
  exitQuiet();                             // quiet ends
  TEST_ASSERT_TRUE(t.step(true, false, 0));
  TEST_ASSERT_FALSE(t.step(true, false, 0));
}

void test_trigger_fires_on_subscribe() {
  StateNotifyTrigger t;
  enterQuiet(false, true);
  t.step(true, false, 0);
  TEST_ASSERT_FALSE(t.step(true, false, 0));
  TEST_ASSERT_TRUE(t.step(true, true, 0));
  TEST_ASSERT_FALSE(t.step(true, false, 0));
}

void test_trigger_is_silent_without_a_client() {
  StateNotifyTrigger t;
  enterQuiet(false, true);
  TEST_ASSERT_FALSE(t.step(false, false, 0));
  TEST_ASSERT_FALSE(t.step(false, true, 0));
  exitQuiet();
  TEST_ASSERT_FALSE(t.step(false, false, 0));
  // A quiet change while nobody is connected is carried by the next connect.
  TEST_ASSERT_TRUE(t.step(true, false, 0));
  TEST_ASSERT_FALSE(t.step(true, false, 0));
}

// A due notify that does not go out (no heap, a refused send) is due
// again kRetryMs later, and only then; one that goes out is not repeated.
void test_failed_quiet_edge_is_retried_after_the_spacing() {
  StateNotifyTrigger t;
  TEST_ASSERT_TRUE(t.step(true, false, 1000));  // connect
  enterQuiet(false, true);
  TEST_ASSERT_TRUE(t.step(true, false, 1300));
  t.failed(1300);
  TEST_ASSERT_FALSE(t.step(true, false, 1300));
  TEST_ASSERT_FALSE(t.step(true, false, 1300 + StateNotifyTrigger::kRetryMs - 1));
  TEST_ASSERT_TRUE(t.step(true, false, 1300 + StateNotifyTrigger::kRetryMs));
  TEST_ASSERT_FALSE(t.step(true, false, 1300 + 5 * StateNotifyTrigger::kRetryMs));
}

void test_failed_connect_notify_is_retried() {
  StateNotifyTrigger t;
  TEST_ASSERT_TRUE(t.step(true, false, 50));
  t.failed(50);
  TEST_ASSERT_FALSE(t.step(true, false, 60));
  TEST_ASSERT_TRUE(t.step(true, false, 50 + StateNotifyTrigger::kRetryMs + 7));
}

// Every failure re-arms; the retry keeps coming until one goes out.
void test_repeated_failures_keep_the_notify_owed() {
  StateNotifyTrigger t;
  enterQuiet(false, true);
  uint32_t now = 0;
  TEST_ASSERT_TRUE(t.step(true, false, now));
  for (int i = 0; i < 4; ++i) {
    t.failed(now);
    now += StateNotifyTrigger::kRetryMs;
    TEST_ASSERT_TRUE(t.step(true, false, now));
  }
  TEST_ASSERT_FALSE(t.step(true, false, now + 3 * StateNotifyTrigger::kRetryMs));
}

// Nothing is retried while no client is connected; the reconnect is due once.
void test_no_retry_without_a_client() {
  StateNotifyTrigger t;
  TEST_ASSERT_TRUE(t.step(true, false, 0));
  t.failed(0);
  TEST_ASSERT_FALSE(t.step(false, false, 5 * StateNotifyTrigger::kRetryMs));
  TEST_ASSERT_TRUE(t.step(true, false, 6 * StateNotifyTrigger::kRetryMs));
  TEST_ASSERT_FALSE(t.step(true, false, 9 * StateNotifyTrigger::kRetryMs));
}

// The spacing is millis()-wrap-safe.
void test_retry_spacing_across_a_millis_wrap() {
  StateNotifyTrigger t;
  const uint32_t at = 0xFFFFFFF0u;
  TEST_ASSERT_TRUE(t.step(true, false, at));
  t.failed(at);
  TEST_ASSERT_FALSE(t.step(true, false, at + StateNotifyTrigger::kRetryMs - 1));
  TEST_ASSERT_TRUE(t.step(true, false, at + StateNotifyTrigger::kRetryMs));
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

// A NimBLE send to one connection, as notify(data, len, connHandle) makes it.
struct FakeSend {
  bool ok = true;
  int calls = 0;
  uint16_t connHandle = 0;
  std::string bytes;
  bool operator()(const uint8_t* data, size_t len, uint16_t handle) {
    ++calls;
    connHandle = handle;
    bytes.assign(reinterpret_cast<const char*>(data), len);
    return ok;
  }
};

static void fillRev7(StateNotifyFields& f, lamp::GreetingState&) { f.nearbyRev = 7; }

// A send to one connection skips NimBLE's subscription check, so a client
// that has not turned notifications on gets nothing, and nothing is built.
void test_unsubscribed_client_gets_nothing() {
  using ble_control::sendStateNotify;
  using ble_control::StateNotifySend;
  FakeSend send;
  bool filled = false;
  const StateNotifySend r = sendStateNotify(
      /*subscribed=*/false, 3,
      [&](StateNotifyFields&, lamp::GreetingState&) { filled = true; },
      [&](const uint8_t* d, size_t n, uint16_t h) { return send(d, n, h); });
  TEST_ASSERT_TRUE(r == StateNotifySend::NotSubscribed);
  TEST_ASSERT_EQUAL(0, send.calls);
  TEST_ASSERT_FALSE(filled);
}

// The subscribed client gets the built payload's bytes, on its own connection.
void test_subscribed_client_gets_the_payload_on_its_connection() {
  using ble_control::sendStateNotify;
  using ble_control::StateNotifySend;
  StateNotifyFields f;
  f.nearbyRev = 7;
  std::string expected;
  buildStateNotifyJson(f, expected);
  FakeSend send;
  const StateNotifySend r = sendStateNotify(
      true, 3, fillRev7, [&](const uint8_t* d, size_t n, uint16_t h) { return send(d, n, h); });
  TEST_ASSERT_TRUE(r == StateNotifySend::Sent);
  TEST_ASSERT_EQUAL(1, send.calls);
  TEST_ASSERT_EQUAL_UINT16(3, send.connHandle);
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), send.bytes.c_str());
}

// A send NimBLE refuses (no mbuf) reads as not sent, so the trigger owes it
// and it goes again kRetryMs later.
void test_refused_send_is_retried() {
  using ble_control::sendStateNotify;
  using ble_control::StateNotifySend;
  StateNotifyTrigger t;
  FakeSend send;
  send.ok = false;
  auto attempt = [&](uint32_t now) {
    if (!t.step(true, false, now)) return false;
    const StateNotifySend r = sendStateNotify(
        true, 3, fillRev7, [&](const uint8_t* d, size_t n, uint16_t h) { return send(d, n, h); });
    if (r != StateNotifySend::Sent) t.failed(now);
    return true;
  };
  TEST_ASSERT_TRUE(attempt(1000));
  TEST_ASSERT_EQUAL(1, send.calls);
  TEST_ASSERT_FALSE(attempt(1000 + StateNotifyTrigger::kRetryMs - 1));
  send.ok = true;
  TEST_ASSERT_TRUE(attempt(1000 + StateNotifyTrigger::kRetryMs));
  TEST_ASSERT_EQUAL(2, send.calls);
  TEST_ASSERT_FALSE(attempt(1000 + 4 * StateNotifyTrigger::kRetryMs));
}

// A refused build sends nothing and says why.
void test_no_heap_sends_nothing() {
  using ble_control::sendStateNotify;
  using ble_control::StateNotifySend;
  FakeSend send;
  g_mock_largest_free_block = lamp::kHeapHeadroom;
  const StateNotifySend r = sendStateNotify(
      true, 3, fillRev7, [&](const uint8_t* d, size_t n, uint16_t h) { return send(d, n, h); });
  g_mock_largest_free_block = 1 << 20;
  TEST_ASSERT_TRUE(r == StateNotifySend::NoHeap);
  TEST_ASSERT_EQUAL(0, send.calls);
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
  RUN_TEST(test_failed_quiet_edge_is_retried_after_the_spacing);
  RUN_TEST(test_failed_connect_notify_is_retried);
  RUN_TEST(test_repeated_failures_keep_the_notify_owed);
  RUN_TEST(test_no_retry_without_a_client);
  RUN_TEST(test_retry_spacing_across_a_millis_wrap);
  RUN_TEST(test_guarded_build_matches_and_refuses_on_heap_failure);
  RUN_TEST(test_unsubscribed_client_gets_nothing);
  RUN_TEST(test_subscribed_client_gets_the_payload_on_its_connection);
  RUN_TEST(test_refused_send_is_retried);
  RUN_TEST(test_no_heap_sends_nothing);
  return UNITY_END();
}
