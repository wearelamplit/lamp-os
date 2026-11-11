// Native-host unit tests for mergeWispStatusJson, the JSON served on
// CHAR_WISP_STATUS (capped at the 512 B ATT ceiling) and on the `wispstatus`
// page section (whole).

#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

// Native-test seam: include the .cpp to get the definitions.
#include "components/network/mesh/wisp_status_merge.cpp"

using lamp::kWispStatusCharMaxLen;
using lamp::mergeWispStatusJson;
using lamp::isJsonObject;
using lamp::storeIfChanged;
using lamp::WispStatusMergeInput;

namespace {

// The wisp's widest status once the config copy is gone (test_status_json's
// status-only worst case on the wisp side): Off mode, every live field set.
const char kStatusOnlyWorst[] =
    R"({"char":"wispStatus","source":"off","wifiConnected":false,)"
    R"("sourceLive":false,"lastSeenMs":9223372036854775,"hasPassword":true,)"
    R"("offColor":"ffffffff","oscRole":"secondary","paletteIdPrefix":"abcdef12",)"
    R"("name":"12345678901234567890","ethIp":"255.255.255.255",)"
    R"("wifiChannelMismatch":true,"wifiApChannel":11})";

// Every lamp-merged field at its widest.
WispStatusMergeInput widestMerge(const char* status) {
  WispStatusMergeInput in;
  in.present = true;
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  std::memcpy(in.mac, mac, 6);
  in.wispVersion = 4294967295u;
  in.flags = 255;
  in.lastHelloMs = 4294967295u;
  std::memcpy(in.helloPaletteIdPrefix, "abcdefgh", 9);
  in.statusJson = status;
  in.lastStatusMs = 4294967295u;
  in.lastConfigMs = 4294967295u;
  in.haveLampState = true;
  in.baseWispColor = "#ffffffff";
  in.shadeWispColor = "#ffffffff";
  return in;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_nothing_known_serves_empty_object() {
  WispStatusMergeInput in;
  TEST_ASSERT_EQUAL_STRING("{}", mergeWispStatusJson(in, 0).c_str());
}

// configLastSeenMs rides only once a wispConfig has been cached; it is the
// app's cue to re-read the wispconfig section.
void test_config_stamp_merged_only_when_config_cached() {
  WispStatusMergeInput in = widestMerge(R"({"char":"wispStatus","source":"osc"})");
  in.lastConfigMs = 0;
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, mergeWispStatusJson(in, 0)));
  TEST_ASSERT_TRUE(d["configLastSeenMs"].isNull());

  in.lastConfigMs = 777;
  JsonDocument d2;
  TEST_ASSERT_FALSE(deserializeJson(d2, mergeWispStatusJson(in, 0)));
  TEST_ASSERT_EQUAL_UINT32(777, d2["configLastSeenMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", d2["wispMac"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("osc", d2["source"].as<const char*>());
}

// The merge adds 301 B at worst, so even the wisp's status with no config copy
// overflows the 512 B characteristic whole; the page section serves all of it.
void test_uncapped_merge_of_status_only_worst_case_pinned() {
  TEST_ASSERT_EQUAL_size_t(301, std::strlen(kStatusOnlyWorst));
  const std::string out = mergeWispStatusJson(widestMerge(kStatusOnlyWorst), 0);
  TEST_ASSERT_EQUAL_size_t(602, out.size());
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["helloPaletteIdPrefix"].isNull());
  TEST_ASSERT_FALSE(d["configLastSeenMs"].isNull());
}

// Capped for CHAR_WISP_STATUS, the diagnostics shed in order until the
// document fits; every field an app reads survives.
void test_capped_merge_sheds_diagnostics_to_fit() {
  const std::string out = mergeWispStatusJson(widestMerge(kStatusOnlyWorst),
                                              kWispStatusCharMaxLen);
  TEST_ASSERT_EQUAL_size_t(509, out.size());
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["helloPaletteIdPrefix"].isNull());
  TEST_ASSERT_TRUE(d["helloLastSeenMs"].isNull());
  TEST_ASSERT_TRUE(d["statusLastSeenMs"].isNull());
  TEST_ASSERT_FALSE(d["configLastSeenMs"].isNull());
  TEST_ASSERT_FALSE(d["helloFlags"].isNull());
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", d["wispMac"].as<const char*>());
  TEST_ASSERT_FALSE(d["wispVersion"].isNull());
  TEST_ASSERT_FALSE(d["shadeWispColor"].isNull());
  TEST_ASSERT_EQUAL_STRING("12345678901234567890", d["name"].as<const char*>());
}

// A document that already fits sheds nothing.
void test_capped_merge_keeps_diagnostics_when_it_fits() {
  WispStatusMergeInput in =
      widestMerge(R"({"char":"wispStatus","source":"osc"})");
  JsonDocument d;
  TEST_ASSERT_FALSE(
      deserializeJson(d, mergeWispStatusJson(in, kWispStatusCharMaxLen)));
  TEST_ASSERT_FALSE(d["helloPaletteIdPrefix"].isNull());
  TEST_ASSERT_FALSE(d["statusLastSeenMs"].isNull());
}

// Keys the wisp's payload carries win over the lamp's merge.
void test_status_payload_keys_win() {
  WispStatusMergeInput in =
      widestMerge(R"({"char":"wispStatus","wispVersion":7})");
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, mergeWispStatusJson(in, 0)));
  TEST_ASSERT_EQUAL_UINT32(7, d["wispVersion"].as<uint32_t>());
}

// A re-sent identical config leaves the cache alone, so configLastSeenMs and
// the app's re-read move only on a real change.
void test_store_if_changed_only_on_new_bytes() {
  std::string cached;
  const char a[] = R"({"char":"wispConfig","px":30})";
  const char b[] = R"({"char":"wispConfig","px":31})";
  TEST_ASSERT_TRUE(storeIfChanged(cached, a, sizeof(a) - 1));
  TEST_ASSERT_FALSE(storeIfChanged(cached, a, sizeof(a) - 1));
  TEST_ASSERT_TRUE(storeIfChanged(cached, b, sizeof(b) - 1));
  TEST_ASSERT_EQUAL_STRING(b, cached.c_str());
  TEST_ASSERT_TRUE(storeIfChanged(cached, b, sizeof(b) - 2));
}

// A malformed or non-object wispConfig never reaches the cache.
void test_is_json_object_rejects_malformed() {
  const char ok[] = R"({"char":"wispConfig","px":30})";
  const char cut[] = R"({"char":"wispConfig","px":3)";
  const char arr[] = R"([1,2])";
  TEST_ASSERT_TRUE(isJsonObject(ok, sizeof(ok) - 1));
  TEST_ASSERT_FALSE(isJsonObject(cut, sizeof(cut) - 1));
  TEST_ASSERT_FALSE(isJsonObject(arr, sizeof(arr) - 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_known_serves_empty_object);
  RUN_TEST(test_config_stamp_merged_only_when_config_cached);
  RUN_TEST(test_uncapped_merge_of_status_only_worst_case_pinned);
  RUN_TEST(test_capped_merge_sheds_diagnostics_to_fit);
  RUN_TEST(test_capped_merge_keeps_diagnostics_when_it_fits);
  RUN_TEST(test_status_payload_keys_win);
  RUN_TEST(test_store_if_changed_only_on_new_bytes);
  RUN_TEST(test_is_json_object_rejects_malformed);
  return UNITY_END();
}
