// Native-host test for the persist-time validation guard. A web whole-document
// PUT once serialized into an Arduino String with no OOM guard; on a fragmented
// heap the String silently truncated and persistRawJson wrote the partial blob
// straight over a good config, factory-resetting the lamp on next boot.
//
// persistRawJson now gates on config_codec::configBlobPersistable before
// touching NVS. Config itself can't link natively (store + NimBLE), so this
// exercises the real predicate against the real InMemoryConfigStore through the
// same one-line gate persistRawJson applies.

#include <unity.h>

#include "config/config_store.hpp"
#include "config/config_codec.hpp"

// Native tests don't build src/, so compile the real implementations in.
#include "../../src/util/color.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

// Mirror persistRawJson's write gate exactly.
static bool persist(InMemoryConfigStore& s, const char* json) {
  if (!config_codec::configBlobPersistable(json)) return false;
  return s.write("cfg", json) > 0;
}

static const char* kGood = R"({"lamp":{"name":"jacko","named":true}})";
static const char* kTruncated = R"({"lamp":{"name":"jac)";  // silent-OOM shape
static const char* kNoLampObject = R"({"base":{"px":32}})";

void test_truncated_blob_rejected_and_store_untouched() {
  InMemoryConfigStore s;
  TEST_ASSERT_TRUE(persist(s, kGood));
  TEST_ASSERT_EQUAL_STRING(kGood, s.read("cfg", "").c_str());

  TEST_ASSERT_FALSE(persist(s, kTruncated));
  TEST_ASSERT_EQUAL_STRING(kGood, s.read("cfg", "").c_str());
}

void test_blob_without_lamp_object_rejected() {
  InMemoryConfigStore s;
  TEST_ASSERT_TRUE(persist(s, kGood));
  TEST_ASSERT_FALSE(persist(s, kNoLampObject));
  TEST_ASSERT_EQUAL_STRING(kGood, s.read("cfg", "").c_str());
}

void test_valid_whole_document_accepted() {
  InMemoryConfigStore s;
  TEST_ASSERT_TRUE(persist(s, kGood));
  TEST_ASSERT_EQUAL_STRING(kGood, s.read("cfg", "").c_str());
}

void test_null_rejected() {
  TEST_ASSERT_FALSE(config_codec::configBlobPersistable(nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_truncated_blob_rejected_and_store_untouched);
  RUN_TEST(test_blob_without_lamp_object_rejected);
  RUN_TEST(test_valid_whole_document_accepted);
  RUN_TEST(test_null_rejected);
  return UNITY_END();
}
