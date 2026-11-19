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

// The wisp's widest status (test_status_json's worst case on the wisp side):
// Off mode, every live field set.
const char kWorstStatus[] =
    R"({"char":"wispStatus","source":"off","wifiConnected":false,)"
    R"("sourceLive":false,"lastSeenMs":9223372036854775,"configSplit":true,)"
    R"("hasPassword":true,"manualId":"abcdef12","manualSeq":65535,)"
    R"("swatchesSeq":65535,)"
    R"("offColor":"ffffffff","oscRole":"secondary","paletteIdPrefix":"abcdef12",)"
    R"("name":"12345678901234567890","ethIp":"255.255.255.255",)"
    R"("wifiChannelMismatch":true,"wifiApChannel":11})";

// Every lamp-merged field at its widest; a controlling flag rides only when
// true.
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
  in.controllingBase = true;
  in.controllingShade = true;
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

// The merge adds 299 B at worst and drops the 20 B "char" tag, so the wisp's
// widest status overflows the 512 B characteristic whole; the page section
// serves all of it.
void test_uncapped_merge_of_worst_status_pinned() {
  TEST_ASSERT_EQUAL_size_t(380, std::strlen(kWorstStatus));
  const std::string out = mergeWispStatusJson(widestMerge(kWorstStatus), 0);
  TEST_ASSERT_EQUAL_size_t(659, out.size());
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["helloPaletteIdPrefix"].isNull());
  TEST_ASSERT_FALSE(d["configLastSeenMs"].isNull());
}

// Capped for CHAR_WISP_STATUS, the diagnostics shed in order. The widest
// status is still over 512 B with all four gone, so its notify goes blank
// and the app re-reads the page section. Every field an app reads survives,
// configSplit among them, since without it the app reads the settings as
// parser defaults, and manualId, manualSeq and swatchesSeq, whose absence
// reads as an older wisp or a pick not yet applied.
void test_capped_merge_sheds_every_diagnostic() {
  const std::string out = mergeWispStatusJson(widestMerge(kWorstStatus),
                                              kWispStatusCharMaxLen);
  TEST_ASSERT_EQUAL_size_t(549, out.size());
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["helloPaletteIdPrefix"].isNull());
  TEST_ASSERT_TRUE(d["helloLastSeenMs"].isNull());
  TEST_ASSERT_TRUE(d["statusLastSeenMs"].isNull());
  TEST_ASSERT_FALSE(d["configLastSeenMs"].isNull());
  TEST_ASSERT_TRUE(d["helloFlags"].isNull());
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", d["wispMac"].as<const char*>());
  TEST_ASSERT_FALSE(d["wispVersion"].isNull());
  TEST_ASSERT_FALSE(d["shadeWispColor"].isNull());
  TEST_ASSERT_EQUAL_STRING("12345678901234567890", d["name"].as<const char*>());
  TEST_ASSERT_TRUE(d["configSplit"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["manualId"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(65535, d["manualSeq"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(65535, d["swatchesSeq"].as<uint32_t>());
}

// A Manual status without Ethernet and with a 13-character name, after a
// seq'd swatches pick, fits only once all four diagnostics are gone. Before
// any such pick it fits once three are gone, so the shedding stops there and
// helloFlags stays.
void test_capped_merge_stops_shedding_once_it_fits() {
  std::string status(kWorstStatus);
  status.replace(status.find(R"("source":"off")"), 14, R"("source":"manual")");
  status.erase(status.find(R"("offColor":"ffffffff",)"), 22);
  status.erase(status.find(R"("ethIp":"255.255.255.255",)"), 26);
  status.replace(status.find("12345678901234567890"), 20, "1234567890123");
  const std::string out =
      mergeWispStatusJson(widestMerge(status.c_str()), kWispStatusCharMaxLen);
  TEST_ASSERT_EQUAL_size_t(497, out.size());
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["helloFlags"].isNull());
  TEST_ASSERT_EQUAL_UINT32(65535, d["swatchesSeq"].as<uint32_t>());

  status.erase(status.find(R"("swatchesSeq":65535,)"), 20);
  const std::string unpicked =
      mergeWispStatusJson(widestMerge(status.c_str()), kWispStatusCharMaxLen);
  TEST_ASSERT_EQUAL_size_t(494, unpicked.size());
  JsonDocument d2;
  TEST_ASSERT_FALSE(deserializeJson(d2, unpicked));
  TEST_ASSERT_TRUE(d2["statusLastSeenMs"].isNull());
  TEST_ASSERT_FALSE(d2["helloFlags"].isNull());
  TEST_ASSERT_TRUE(d2["configSplit"].as<bool>());
}

// A name of 20 quotes, escaped to 40 bytes, still overflows after every
// diagnostic sheds; configSplit stays even then.
void test_capped_merge_keeps_config_split_when_still_over() {
  std::string status(kWorstStatus);
  status.replace(status.find("12345678901234567890"), 20,
                 R"(\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\")");
  const std::string out =
      mergeWispStatusJson(widestMerge(status.c_str()), kWispStatusCharMaxLen);
  TEST_ASSERT_EQUAL_size_t(569, out.size());
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["helloFlags"].isNull());
  TEST_ASSERT_TRUE(d["configSplit"].as<bool>());
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

// The served document never carries the mesh frame's "char" tag, whether it
// merges a cached payload, only a hello, or a payload that does not parse.
// The cached payload keeps it.
void test_merge_drops_char_tag() {
  WispStatusMergeInput withPayload = widestMerge(kWorstStatus);
  WispStatusMergeInput helloOnly = widestMerge("");
  WispStatusMergeInput malformed = widestMerge(R"({"char":"wispStatus","source")");
  for (const WispStatusMergeInput* in : {&withPayload, &helloOnly, &malformed}) {
    const std::string out = mergeWispStatusJson(*in, 0);
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, out));
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", d["wispMac"].as<const char*>());
    TEST_ASSERT_EQUAL(std::string::npos, out.find("\"char\""));
  }
  TEST_ASSERT_EQUAL(0u, withPayload.statusJson.find(R"({"char":"wispStatus")"));
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, mergeWispStatusJson(withPayload, 0)));
  TEST_ASSERT_EQUAL_STRING("off", d["source"].as<const char*>());
}

// A controlling flag rides only when true; the lamp's state overrides a key
// the payload carries, and the heading colours are untouched.
void test_controlling_flags_only_when_true() {
  WispStatusMergeInput in =
      widestMerge(R"({"char":"wispStatus","controllingShade":true})");
  in.controllingShade = false;
  const std::string out = mergeWispStatusJson(in, 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["controllingBase"].as<bool>());
  TEST_ASSERT_EQUAL(std::string::npos, out.find("controllingShade"));
  TEST_ASSERT_EQUAL_STRING("#ffffffff", d["baseWispColor"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("#ffffffff", d["shadeWispColor"].as<const char*>());

  in.controllingBase = false;
  in.controllingShade = true;
  const std::string swapped = mergeWispStatusJson(in, 0);
  JsonDocument d2;
  TEST_ASSERT_FALSE(deserializeJson(d2, swapped));
  TEST_ASSERT_EQUAL(std::string::npos, swapped.find("controllingBase"));
  TEST_ASSERT_TRUE(d2["controllingShade"].as<bool>());
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
  RUN_TEST(test_uncapped_merge_of_worst_status_pinned);
  RUN_TEST(test_capped_merge_sheds_every_diagnostic);
  RUN_TEST(test_capped_merge_stops_shedding_once_it_fits);
  RUN_TEST(test_capped_merge_keeps_config_split_when_still_over);
  RUN_TEST(test_capped_merge_keeps_diagnostics_when_it_fits);
  RUN_TEST(test_status_payload_keys_win);
  RUN_TEST(test_merge_drops_char_tag);
  RUN_TEST(test_controlling_flags_only_when_true);
  RUN_TEST(test_store_if_changed_only_on_new_bytes);
  RUN_TEST(test_is_json_object_rejects_malformed);
  return UNITY_END();
}
