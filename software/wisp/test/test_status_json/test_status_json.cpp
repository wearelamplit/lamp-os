#include <unity.h>
#include <ArduinoJson.h>
#include <cstdint>
#include "status/status_json.hpp"
#include "wire/lamp_protocol.hpp"

constexpr size_t CAP = 230;

// Guaranteed-core ceiling at worst-case field widths. If this grows past
// 179 the degrade-by-construction guarantee shrinks; re-derive the slack
// before bumping.
constexpr size_t CORE_PIN = 179;

void setUp() {}
void tearDown() {}

// Guaranteed core at pathological widths: 11-char currentZone, widest
// zoneSource, 10-digit lastSeenMs, widest source, hasPassword present.
// All optional fields at their app defaults (omitted).
void test_core_worst_case_pinned_under_cap() {
  wisp::WispStatusFields f{ INT32_MIN, "firstSeen", nullptr, 0,
                            true, true, "", 4294967295u, "manual",
                            0, 0, 0, 0, false, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/true,
                            /*ledType=*/"GRB", /*pixelCount=*/30 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n <= CORE_PIN);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("wispStatus", d["char"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("manual", d["source"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("firstSeen", d["zoneSource"].as<const char*>());
  TEST_ASSERT_FALSE(d["currentZone"].isNull());
  TEST_ASSERT_FALSE(d["wifiConnected"].isNull());
  TEST_ASSERT_FALSE(d["auroraConnected"].isNull());
  TEST_ASSERT_FALSE(d["lastSeenMs"].isNull());
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
}

// Every optional field set at pathological widths: the frame is always
// produced (never 0) and stays under cap; the core survives intact.
void test_fully_loaded_worst_case_never_fails() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;
  wisp::WispStatusFields f{ INT32_MIN, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "aurora",
                            255, 255, 255, 255, true, /*shuffleSeed=*/255,
                            /*driftIntervalMs=*/3600000, /*driftFadePct=*/100,
                            /*name=*/"12345678901234567890",
                            /*hasPassword=*/true,
                            /*ledType=*/"GRBW", /*pixelCount=*/65535,
                            /*rangeStep=*/3, /*opSeq=*/4294967295u };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["source"].isNull());
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
}

// Priority: offColor and paletteIdPrefix survive budget pressure that
// drops the widest cosmetic (ledType). currentZone stays realistic here.
void test_offcolor_and_prefix_outrank_cosmetics() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;
  wisp::WispStatusFields f{ 3, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "off",
                            255, 255, 255, 255, true, /*shuffleSeed=*/255,
                            /*driftIntervalMs=*/3600000, /*driftFadePct=*/100,
                            /*name=*/"12345678901234567890",
                            /*hasPassword=*/true,
                            /*ledType=*/"GRBW", /*pixelCount=*/65535 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["offColor"].isNull());
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["paletteIdPrefix"].as<const char*>());
  TEST_ASSERT_TRUE(d["ledType"].isNull());
}

// Omit-when-default: every field whose absence the app parser reconstructs
// is dropped from the wire when at its default.
void test_defaults_omitted() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "", 1000u, "manual",
                            0, 0, 0, 0, false, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false,
                            /*ledType=*/"GRB", /*pixelCount=*/30,
                            /*rangeStep=*/0 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["hasPassword"].isNull());
  TEST_ASSERT_TRUE(d["shuffleSeed"].isNull());
  TEST_ASSERT_TRUE(d["driftIntervalMs"].isNull());
  TEST_ASSERT_TRUE(d["driftFadePct"].isNull());
  TEST_ASSERT_TRUE(d["name"].isNull());
  TEST_ASSERT_TRUE(d["ledType"].isNull());
  TEST_ASSERT_TRUE(d["px"].isNull());
  TEST_ASSERT_TRUE(d["paletteIdPrefix"].isNull());
  TEST_ASSERT_TRUE(d["observedZones"].isNull());
  TEST_ASSERT_TRUE(d["range"].isNull());
  TEST_ASSERT_TRUE(d["opSeq"].isNull());
}

// Non-default identity fields ride the wire when the frame has room.
void test_non_default_identity_fields_emitted() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "", 1000u, "manual",
                            0, 0, 0, 0, false, /*shuffleSeed=*/42,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"lamp-room", /*hasPassword=*/true,
                            /*ledType=*/"BGR", /*pixelCount=*/42,
                            /*rangeStep=*/2 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
  TEST_ASSERT_EQUAL_INT(42, d["shuffleSeed"].as<int>());
  TEST_ASSERT_EQUAL_STRING("lamp-room", d["name"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("BGR", d["ledType"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(42, d["px"].as<int>());
  TEST_ASSERT_EQUAL_INT(2, d["range"].as<int>());
}

// opSeq (sealed-op confirmation counter) rides the wire when non-default and
// the frame has room; omitted at 0.
void test_op_seq_emitted() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "", 1000u, "manual",
                            0, 0, 0, 0, false, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/true,
                            /*ledType=*/"GRB", /*pixelCount=*/30,
                            /*rangeStep=*/0, /*opSeq=*/9 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_UINT32(9, d["opSeq"].as<uint32_t>());
}

// Non-default palette and drift fields ride the wire when the frame has room.
void test_non_default_palette_and_drift_emitted() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "manual",
                            0, 0, 0, 0, false, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/90000, /*driftFadePct=*/40,
                            /*name=*/"", /*hasPassword=*/false,
                            /*ledType=*/"GRB", /*pixelCount=*/30 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["paletteIdPrefix"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(90000, d["driftIntervalMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(40, d["driftFadePct"].as<int>());
}

// Off mode emits offColor and suppresses the drift fields even when they
// are non-default.
void test_off_mode_emits_offcolor_not_drift() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "", 1000u, "off",
                            200, 100, 50, 25, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/90000, /*driftFadePct=*/40,
                            /*name=*/"", /*hasPassword=*/false,
                            /*ledType=*/"GRB", /*pixelCount=*/30 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["offColor"].isNull());
  TEST_ASSERT_EQUAL_STRING("c8643219", d["offColor"].as<const char*>());
  TEST_ASSERT_TRUE(d["driftIntervalMs"].isNull());
  TEST_ASSERT_TRUE(d["driftFadePct"].isNull());
}

// observedZones truncates greedily under budget pressure instead of
// failing the frame.
void test_observed_zones_truncate_greedily() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;
  wisp::WispStatusFields f{ 3, "nvs", zones, 16,
                            false, false, "", 1000u, "manual",
                            0, 0, 0, 0, false, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false,
                            /*ledType=*/"GRB", /*pixelCount=*/30 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  JsonArrayConst z = d["observedZones"].as<JsonArrayConst>();
  TEST_ASSERT_TRUE(z.size() > 0);
  TEST_ASSERT_TRUE(z.size() < 16);
}

// brightness omitted at the 100 default, emitted when the space is dimmed.
void test_brightness_omitted_at_default_emitted_when_dimmed() {
  wisp::WispStatusFields fDefault{ 3, "nvs", nullptr, 0,
                                   false, false, "", 1000u, "manual",
                                   0, 0, 0, 0, false, /*shuffleSeed=*/0,
                                   /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                                   /*name=*/"", /*hasPassword=*/false,
                                   /*ledType=*/"GRB", /*pixelCount=*/30 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(fDefault, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["brightness"].isNull());

  wisp::WispStatusFields fDimmed = fDefault;
  fDimmed.brightness = 40;
  n = wisp::buildWispStatusJson(fDimmed, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d2;
  TEST_ASSERT_FALSE(deserializeJson(d2, out));
  TEST_ASSERT_EQUAL_INT(40, d2["brightness"].as<int>());
}

// Untruncated worst case: every field at its widest non-default value, all
// 16 observed zones, a generous cap so nothing gets dropped. Pins the number
// CONTROL_MAX_PAYLOAD is sized from (see docs/dev/networking.md).
void test_true_worst_case_untruncated_length() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;
  wisp::WispStatusFields f{ INT32_MIN, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "aurora",
                            255, 255, 255, 255, true, /*shuffleSeed=*/255,
                            /*driftIntervalMs=*/3600000, /*driftFadePct=*/100,
                            /*name=*/"12345678901234567890",
                            /*hasPassword=*/true,
                            /*ledType=*/"GRBW", /*pixelCount=*/65535,
                            /*rangeStep=*/255, /*opSeq=*/4294967295u,
                            /*brightness=*/99 };
  char out[1024];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), /*cap=*/4096);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_UINT32(568, n);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  JsonArrayConst z = d["observedZones"].as<JsonArrayConst>();
  TEST_ASSERT_EQUAL_UINT32(16, z.size());
}

// The real emitter path: worst-case fields built at the production cap
// (CONTROL_MAX_PAYLOAD), fed straight into buildControlOp. All 16 zones
// must survive and the frame must build (the whole point of the raised cap).
void test_worst_case_fits_control_op_at_production_cap() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;
  wisp::WispStatusFields f{ INT32_MIN, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "aurora",
                            255, 255, 255, 255, true, /*shuffleSeed=*/255,
                            /*driftIntervalMs=*/3600000, /*driftFadePct=*/100,
                            /*name=*/"12345678901234567890",
                            /*hasPassword=*/true,
                            /*ledType=*/"GRBW", /*pixelCount=*/65535,
                            /*rangeStep=*/255, /*opSeq=*/4294967295u,
                            /*brightness=*/99 };
  char json[1024];
  size_t jsonLen = wisp::buildWispStatusJson(
      f, json, sizeof(json), lamp_protocol::CONTROL_MAX_PAYLOAD);
  TEST_ASSERT_TRUE(jsonLen > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, json));
  JsonArrayConst z = d["observedZones"].as<JsonArrayConst>();
  TEST_ASSERT_EQUAL_UINT32(16, z.size());

  static const uint8_t kTarget[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  static const uint8_t kSrc[6]    = {0xDE,0xAD,0xBE,0xEF,0x00,0x01};
  uint8_t frame[lamp_protocol::CONTROL_MAX_SIZE];
  size_t frameLen = lamp_protocol::buildControlOp(
      frame, sizeof(frame), /*seq=*/1, kTarget, kSrc,
      reinterpret_cast<const uint8_t*>(json), jsonLen);
  TEST_ASSERT_TRUE(frameLen > 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_core_worst_case_pinned_under_cap);
  RUN_TEST(test_true_worst_case_untruncated_length);
  RUN_TEST(test_worst_case_fits_control_op_at_production_cap);
  RUN_TEST(test_brightness_omitted_at_default_emitted_when_dimmed);
  RUN_TEST(test_fully_loaded_worst_case_never_fails);
  RUN_TEST(test_offcolor_and_prefix_outrank_cosmetics);
  RUN_TEST(test_defaults_omitted);
  RUN_TEST(test_non_default_identity_fields_emitted);
  RUN_TEST(test_op_seq_emitted);
  RUN_TEST(test_non_default_palette_and_drift_emitted);
  RUN_TEST(test_off_mode_emits_offcolor_not_drift);
  RUN_TEST(test_observed_zones_truncate_greedily);
  return UNITY_END();
}
