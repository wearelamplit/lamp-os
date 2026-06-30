// Native-host tests for DispositionStore: get/set/clamp/eviction, the JSON
// bulk replace, and a persist/reload round trip through an InMemoryConfigStore
// (real persistence logic, no flash).

#include <unity.h>

#include <cstdio>

#include "config/config_store.hpp"
#include "config/disposition_store.hpp"

// Native tests don't build src/, so compile the real implementation in.
#include "../../src/config/disposition_store.cpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

void test_unknown_peer_returns_default() {
  DispositionStore ds(100);
  TEST_ASSERT_EQUAL_UINT8(DispositionStore::kDefault, ds.get("AA:BB:CC:DD:EE:FF"));
}

void test_set_get_and_clamp() {
  DispositionStore ds(100);
  ds.set("AA:BB:CC:DD:EE:01", 4, 0);
  TEST_ASSERT_EQUAL_UINT8(4, ds.get("AA:BB:CC:DD:EE:01"));
  ds.set("AA:BB:CC:DD:EE:02", 9, 0);  // clamps to 5
  TEST_ASSERT_EQUAL_UINT8(5, ds.get("AA:BB:CC:DD:EE:02"));
  ds.set("AA:BB:CC:DD:EE:03", 0, 0);  // clamps to 1
  TEST_ASSERT_EQUAL_UINT8(1, ds.get("AA:BB:CC:DD:EE:03"));
}

void test_eviction_at_capacity_drops_lowest_key() {
  DispositionStore ds(100);
  char buf[18];
  for (int i = 0; i < 100; i++) {
    snprintf(buf, sizeof(buf), "AA:BB:CC:DD:EE:%02X", i);
    ds.set(buf, 2, 0);
  }
  ds.set("AA:BB:CC:DD:EE:00", 1, 0);  // distinct value on the lowest key
  // New key at capacity → evict the lowest-by-key entry (..:00).
  ds.set("FF:FF:FF:FF:FF:FF", 5, 0);
  TEST_ASSERT_EQUAL_UINT8(DispositionStore::kDefault,
                          ds.get("AA:BB:CC:DD:EE:00"));  // evicted
  TEST_ASSERT_EQUAL_UINT8(2, ds.get("AA:BB:CC:DD:EE:01"));  // kept
  TEST_ASSERT_EQUAL_UINT8(5, ds.get("FF:FF:FF:FF:FF:FF"));  // inserted
}

void test_set_from_json_filters_invalid_keys() {
  DispositionStore ds(100);
  const char* json =
      "{\"AA:BB:CC:DD:EE:01\":4,\"not-a-bdaddr\":2,\"AA:BB:CC:DD:EE:02\":5}";
  TEST_ASSERT_TRUE(ds.setFromJson(json, std::string(json).size(), 0));
  TEST_ASSERT_EQUAL_UINT8(4, ds.get("AA:BB:CC:DD:EE:01"));
  TEST_ASSERT_EQUAL_UINT8(5, ds.get("AA:BB:CC:DD:EE:02"));
  TEST_ASSERT_EQUAL_UINT8(DispositionStore::kDefault, ds.get("not-a-bdaddr"));
}

void test_debounced_persist_and_reload_round_trip() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);  // 100 ms idle window
  ds.attachStore(&backing);

  ds.set("AA:BB:CC:DD:EE:01", 4, 1000);
  ds.maybeFlush(1050);  // within window → no write yet
  TEST_ASSERT_EQUAL_STRING("MISS", backing.read("dispositions", "MISS").c_str());
  ds.maybeFlush(1100);  // window elapsed → writes
  TEST_ASSERT_TRUE(backing.read("dispositions", "MISS") != "MISS");

  // A fresh store over the same backing loads the persisted value.
  DispositionStore reloaded(100);
  reloaded.attachStore(&backing);
  reloaded.load();
  TEST_ASSERT_EQUAL_UINT8(4, reloaded.get("AA:BB:CC:DD:EE:01"));
}

void test_flush_now_writes_immediately() {
  InMemoryConfigStore backing;
  DispositionStore ds(100000);  // long window; only flushNow should write
  ds.attachStore(&backing);
  ds.set("AA:BB:CC:DD:EE:09", 3, 0);
  ds.flushNow();
  TEST_ASSERT_TRUE(backing.read("dispositions", "MISS") != "MISS");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_unknown_peer_returns_default);
  RUN_TEST(test_set_get_and_clamp);
  RUN_TEST(test_eviction_at_capacity_drops_lowest_key);
  RUN_TEST(test_set_from_json_filters_invalid_keys);
  RUN_TEST(test_debounced_persist_and_reload_round_trip);
  RUN_TEST(test_flush_now_writes_immediately);
  return UNITY_END();
}
