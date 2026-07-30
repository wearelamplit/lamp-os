// Native-host test for the data-loss guard: a config blob that was PRESENT in
// NVS but failed to parse must NOT be overwritten by first-boot housekeeping.
// A transient/at-rest parse hiccup (fragmented-heap NoMemory, a partially
// written blob) once cemented itself into a permanent factory reset by
// persisting defaults over the still-recoverable blob.
//
// The full Config load ctor can't link natively (Arduino/Preferences/NimBLE),
// so this exercises the two pure predicates the fix routes through
// (configBlobHasData + shouldPersistFirstBoot), running a real ArduinoJson
// parse over each blob to mirror the constructor faithfully.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "config/config_types.hpp"

void setUp(void) {}
void tearDown(void) {}

// Mirror config.cpp: given the stored blob, did the load fail over real data?
static bool loadFailedWithData(const std::string& stored) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, stored);
  return err && lamp::configBlobHasData(stored);
}

// Present-but-unparseable: serve defaults, but the first-boot persist must be
// suppressed so NVS is left intact.
void test_corrupt_blob_serves_defaults_without_persist() {
  const std::string corrupt = R"({"lamp":{"name":"murky","password":"hunter2)";
  TEST_ASSERT_TRUE(loadFailedWithData(corrupt));
  // wouldPersist is true (housekeeping wants to write), but the guard vetoes it.
  TEST_ASSERT_FALSE(lamp::shouldPersistFirstBoot(true, loadFailedWithData(corrupt)));
}

// Genuine first boot (absent key -> "{}" sentinel, or an empty value): normal
// behavior, defaults + persist allowed.
void test_empty_object_is_true_first_boot() {
  TEST_ASSERT_FALSE(loadFailedWithData("{}"));
  TEST_ASSERT_TRUE(lamp::shouldPersistFirstBoot(true, loadFailedWithData("{}")));
}

void test_empty_string_is_true_first_boot() {
  TEST_ASSERT_FALSE(loadFailedWithData(""));
  TEST_ASSERT_TRUE(lamp::shouldPersistFirstBoot(true, loadFailedWithData("")));
}

// A well-formed stored blob parses fine: never flagged, and housekeeping's own
// wouldPersist=false (adopted lamp) still means no write.
void test_valid_blob_not_flagged() {
  const std::string ok = R"({"lamp":{"name":"murky"}})";
  TEST_ASSERT_FALSE(loadFailedWithData(ok));
  TEST_ASSERT_FALSE(lamp::shouldPersistFirstBoot(false, false));
}

void test_blob_has_data_boundaries() {
  TEST_ASSERT_FALSE(lamp::configBlobHasData(""));
  TEST_ASSERT_FALSE(lamp::configBlobHasData("{}"));
  TEST_ASSERT_TRUE(lamp::configBlobHasData("{\"a\":1}"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_corrupt_blob_serves_defaults_without_persist);
  RUN_TEST(test_empty_object_is_true_first_boot);
  RUN_TEST(test_empty_string_is_true_first_boot);
  RUN_TEST(test_valid_blob_not_flagged);
  RUN_TEST(test_blob_has_data_boundaries);
  return UNITY_END();
}
