#include <unity.h>
#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>
#include "status/status_json.hpp"
#include "wire/lamp_protocol.hpp"

constexpr size_t CAP = 230;

// Guaranteed-core ceiling at worst-case field widths. If this grows past
// 179 the degrade-by-construction guarantee shrinks; re-derive the slack
// before bumping.
constexpr size_t CORE_PIN = 179;

// The widest lastSeenMs a wisp can send: esp_timer_get_time() / 1000 at the
// top of its int64 range.
constexpr uint64_t kWidestStampMs = INT64_MAX / 1000;

void setUp() {}
void tearDown() {}

// Every live field at its widest non-default value, Manual mode (widest
// source with oscRole), both mismatch fields and the widest Ethernet address.
static wisp::WispStatusFields worstCase() {
  return wisp::WispStatusFields{
      false, false, "abcdef12", kWidestStampMs, "manual", "secondary",
      255, 255, 255, 255, true,
      /*name=*/"12345678901234567890", /*hasPassword=*/true,
      /*wifiChannelMismatch=*/true, /*wifiApChannel=*/11,
      /*ethIp=*/"255.255.255.255" };
}

// Every config field at its widest non-default value.
static wisp::WispConfigFields worstConfig() {
  wisp::WispConfigFields c;
  c.ledType = "GRBW";
  c.pixelCount = 65535;
  c.brightness = 99;
  c.driftIntervalMs = 3600000;
  c.driftFadePct = 100;
  c.shuffleSeed = 255;
  c.opSeq = 4294967295u;
  return c;
}

// Defaults everywhere, Manual mode.
static wisp::WispStatusFields plain() {
  return wisp::WispStatusFields{
      false, false, "", 1000u, "manual", "primary",
      0, 0, 0, 0, false,
      /*name=*/"", /*hasPassword=*/false };
}

// The production call: the status carries the config copy.
static size_t build(const wisp::WispStatusFields& f,
                    const wisp::WispConfigFields& c, char* out, size_t outCap,
                    size_t cap) {
  return wisp::buildWispStatusJson(f, &c, out, outCap, cap);
}

static size_t build(const wisp::WispStatusFields& f, char* out, size_t outCap,
                    size_t cap) {
  return build(f, wisp::WispConfigFields{}, out, outCap, cap);
}

// Guaranteed core at pathological widths: 16-digit lastSeenMs, widest
// source, both bools false (wider than true), hasPassword present. All
// optional fields at their app defaults (omitted).
void test_core_worst_case_pinned_under_cap() {
  wisp::WispStatusFields f = plain();
  f.lastSeenMs = kWidestStampMs;
  f.hasPassword = true;
  char out[256];
  size_t n = build(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n <= CORE_PIN);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("wispStatus", d["char"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("manual", d["source"].as<const char*>());
  TEST_ASSERT_FALSE(d["wifiConnected"].isNull());
  TEST_ASSERT_FALSE(d["sourceLive"].isNull());
  TEST_ASSERT_FALSE(d["lastSeenMs"].isNull());
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
  TEST_ASSERT_TRUE(d["auroraConnected"].isNull());
  TEST_ASSERT_TRUE(d["currentZone"].isNull());
  TEST_ASSERT_TRUE(d["zoneSource"].isNull());
}

// The app keeps the newest lastSeenMs it has heard from a wisp and needs a
// later emission to carry a larger one, so the stamp keeps growing past the
// 49.7 days of uptime at which millis() wraps.
void test_last_seen_ms_keeps_growing_past_the_millis_wrap() {
  constexpr uint64_t kWrap = uint64_t{1} << 32;
  wisp::WispStatusFields f = plain();
  char out[256];

  f.lastSeenMs = kWrap - 1000;
  TEST_ASSERT_TRUE(build(f, out, sizeof(out), CAP) > 0);
  JsonDocument before;
  TEST_ASSERT_FALSE(deserializeJson(before, out));

  f.lastSeenMs = kWrap + 1000;
  TEST_ASSERT_TRUE(build(f, out, sizeof(out), CAP) > 0);
  JsonDocument after;
  TEST_ASSERT_FALSE(deserializeJson(after, out));

  TEST_ASSERT_TRUE(after["lastSeenMs"].as<uint64_t>() >
                   before["lastSeenMs"].as<uint64_t>());
  TEST_ASSERT_TRUE(after["lastSeenMs"].as<uint64_t>() == kWrap + 1000);
}

// sourceLive rides the core both ways, so the app never reads absence.
void test_source_live_always_present() {
  wisp::WispStatusFields f = plain();
  f.source = "osc";
  f.sourceLive = true;
  char out[256];
  TEST_ASSERT_TRUE(build(f, out, sizeof(out), CAP) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["sourceLive"].as<bool>());

  f.sourceLive = false;
  TEST_ASSERT_TRUE(build(f, out, sizeof(out), CAP) > 0);
  JsonDocument d2;
  TEST_ASSERT_FALSE(deserializeJson(d2, out));
  TEST_ASSERT_FALSE(d2["sourceLive"].isNull());
  TEST_ASSERT_FALSE(d2["sourceLive"].as<bool>());
}

// Every optional field set at pathological widths: the frame is always
// produced (never 0) and stays under cap; the core survives intact.
void test_fully_loaded_worst_case_never_fails() {
  char out[256];
  size_t n = build(worstCase(), worstConfig(), out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["source"].isNull());
  TEST_ASSERT_FALSE(d["sourceLive"].isNull());
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
}

// Priority: offColor and paletteIdPrefix survive budget pressure that
// drops the widest cosmetic (ledType).
void test_offcolor_and_prefix_outrank_cosmetics() {
  wisp::WispStatusFields f = worstCase();
  f.source = "off";
  f.oscRole = "primary";
  f.wifiChannelMismatch = false;
  wisp::WispConfigFields c = worstConfig();
  c.brightness = 100;
  c.opSeq = 0;
  char out[256];
  size_t n = build(f, c, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["offColor"].isNull());
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["paletteIdPrefix"].as<const char*>());
  TEST_ASSERT_TRUE(d["oscRole"].isNull());
  TEST_ASSERT_TRUE(d["ledType"].isNull());
}

// Priority: oscRole outranks the cosmetics the same way.
void test_osc_role_outranks_cosmetics() {
  char out[256];
  size_t n = build(worstCase(), worstConfig(), out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("secondary", d["oscRole"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["paletteIdPrefix"].as<const char*>());
  TEST_ASSERT_TRUE(d["offColor"].isNull());
  TEST_ASSERT_TRUE(d["ledType"].isNull());
}

// Under budget pressure every live field outranks the whole config copy:
// lamps that read wispConfig lose nothing, and the copy sheds first.
void test_live_fields_outrank_config_copy() {
  char out[512];
  const size_t cap = 300;
  size_t n = build(worstCase(), worstConfig(), out, sizeof(out), cap);
  TEST_ASSERT_TRUE(n > 0 && n <= cap);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("12345678901234567890", d["name"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("255.255.255.255", d["ethIp"].as<const char*>());
  TEST_ASSERT_TRUE(d["wifiChannelMismatch"].as<bool>());
  TEST_ASSERT_EQUAL_INT(11, d["wifiApChannel"].as<int>());
  TEST_ASSERT_TRUE(d["brightness"].isNull());
  TEST_ASSERT_TRUE(d["px"].isNull());
}

// oscRole is omitted at its "primary" default and rides in every mode off
// it, so the app already shows the stored role when Osc is picked.
void test_osc_role_omitted_at_default_and_emitted_in_every_mode() {
  const char* sources[] = {"off", "manual", "osc"};
  for (const char* src : sources) {
    wisp::WispStatusFields f = plain();
    f.source = src;
    char out[256];
    TEST_ASSERT_TRUE(build(f, out, sizeof(out), CAP) > 0);
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, out));
    TEST_ASSERT_TRUE(d["oscRole"].isNull());

    f.oscRole = "fill";
    TEST_ASSERT_TRUE(build(f, out, sizeof(out), CAP) > 0);
    JsonDocument d2;
    TEST_ASSERT_FALSE(deserializeJson(d2, out));
    TEST_ASSERT_EQUAL_STRING("fill", d2["oscRole"].as<const char*>());
  }
}

// Omit-when-default: every field whose absence the app parser reconstructs
// is dropped from the wire when at its default.
void test_defaults_omitted() {
  char out[256];
  size_t n = build(plain(), out, sizeof(out), CAP);
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
  TEST_ASSERT_TRUE(d["oscRole"].isNull());
  TEST_ASSERT_TRUE(d["opSeq"].isNull());
  TEST_ASSERT_TRUE(d["ethIp"].isNull());
  TEST_ASSERT_TRUE(d["brightness"].isNull());
  TEST_ASSERT_TRUE(d["configSplit"].isNull());
}

// ethIp rides only while Ethernet is up; an empty address is omitted.
void test_eth_ip_emitted_only_when_set() {
  wisp::WispStatusFields f = plain();
  f.ethIp = "";
  char out[256];
  TEST_ASSERT_TRUE(build(f, out, sizeof(out), CAP) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["ethIp"].isNull());

  f.ethIp = "10.0.0.42";
  TEST_ASSERT_TRUE(build(f, out, sizeof(out), CAP) > 0);
  JsonDocument d2;
  TEST_ASSERT_FALSE(deserializeJson(d2, out));
  TEST_ASSERT_EQUAL_STRING("10.0.0.42", d2["ethIp"].as<const char*>());
}

// Non-default identity and config-copy fields ride the wire when the frame
// has room.
void test_non_default_identity_fields_emitted() {
  wisp::WispStatusFields f = plain();
  f.name = "lamp-room";
  f.hasPassword = true;
  wisp::WispConfigFields c;
  c.shuffleSeed = 42;
  c.ledType = "BGR";
  c.pixelCount = 42;
  char out[256];
  size_t n = build(f, c, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
  TEST_ASSERT_EQUAL_INT(42, d["shuffleSeed"].as<int>());
  TEST_ASSERT_EQUAL_STRING("lamp-room", d["name"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("BGR", d["ledType"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(42, d["px"].as<int>());
}

// opSeq (sealed-op confirmation counter) rides the copy when non-default
// and the frame has room; omitted at 0.
void test_op_seq_emitted() {
  wisp::WispStatusFields f = plain();
  f.hasPassword = true;
  wisp::WispConfigFields c;
  c.opSeq = 9;
  char out[256];
  size_t n = build(f, c, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_UINT32(9, d["opSeq"].as<uint32_t>());
}

// Non-default palette and drift fields ride the wire when the frame has room.
void test_non_default_palette_and_drift_emitted() {
  wisp::WispStatusFields f = plain();
  f.paletteIdPrefix = "abcdef12";
  wisp::WispConfigFields c;
  c.driftIntervalMs = 90000;
  c.driftFadePct = 40;
  char out[256];
  size_t n = build(f, c, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["paletteIdPrefix"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(90000, d["driftIntervalMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(40, d["driftFadePct"].as<int>());
}

// Off mode emits offColor and still carries both drift fields in the copy:
// the app's drift form shows in every mode.
void test_off_mode_emits_offcolor_and_drift() {
  wisp::WispStatusFields f = plain();
  f.source = "off";
  f.offR = 200; f.offG = 100; f.offB = 50; f.offW = 25;
  f.hasOffColor = true;
  wisp::WispConfigFields c;
  c.driftIntervalMs = 90000;
  c.driftFadePct = 40;
  char out[256];
  size_t n = build(f, c, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("c8643219", d["offColor"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(90000, d["driftIntervalMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(40, d["driftFadePct"].as<int>());
}

// brightness omitted at the 100 default, emitted when the space is dimmed.
void test_brightness_omitted_at_default_emitted_when_dimmed() {
  char out[256];
  wisp::WispConfigFields c;
  size_t n = build(plain(), c, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["brightness"].isNull());

  c.brightness = 40;
  n = build(plain(), c, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d2;
  TEST_ASSERT_FALSE(deserializeJson(d2, out));
  TEST_ASSERT_EQUAL_INT(40, d2["brightness"].as<int>());
}

// Without a config copy no config field rides the status, whatever its value.
void test_no_config_copy_carries_no_config_field() {
  char out[512];
  size_t n = wisp::buildWispStatusJson(worstCase(), nullptr, out, sizeof(out),
                                       lamp_protocol::CONTROL_MAX_PAYLOAD);
  TEST_ASSERT_TRUE(n > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  const char* configKeys[] = {"ledType", "px", "brightness", "driftIntervalMs",
                              "driftFadePct", "shuffleSeed", "opSeq"};
  for (const char* k : configKeys) TEST_ASSERT_TRUE(d[k].isNull());
}

// Absolute worst case, untruncated: every field widest, Off mode (offColor
// beside the drift fields), both mismatch fields, ethIp, the config copy. It
// fits CONTROL_MAX_PAYLOAD with room to spare, so at the production cap
// nothing sheds. Manual mode, without offColor, is 408.
void test_worst_case_untruncated_length() {
  char out[1024];
  wisp::WispStatusFields f = worstCase();
  f.source = "off";
  size_t n = build(f, worstConfig(), out, sizeof(out), /*cap=*/4096);
  TEST_ASSERT_EQUAL_UINT32(427, n);
  TEST_ASSERT_TRUE(n <= lamp_protocol::CONTROL_MAX_PAYLOAD);
  n = build(worstCase(), worstConfig(), out, sizeof(out), /*cap=*/4096);
  TEST_ASSERT_EQUAL_UINT32(408, n);
}

// The widest status without its config copy: Off mode (offColor outweighs
// the longer "manual"), every live field set. The lamp's merge adds up to
// 301 B on top (test_wisp_status_merge), which is why the app reads it
// through the wispstatus page section rather than the 512 B characteristic.
// ArduinoJson escapes '"', '\' and \b \f \n \r \t to two bytes and passes
// other control bytes raw, so a name of 20 quotes (escaped to 40) is the
// true ceiling.
void test_status_only_worst_case_pinned() {
  wisp::WispStatusFields f = worstCase();
  f.source = "off";
  char out[1024];
  size_t n = wisp::buildWispStatusJson(f, nullptr, out, sizeof(out), 4096);
  TEST_ASSERT_EQUAL_UINT32(301, n);

  f.name = "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"";
  n = wisp::buildWispStatusJson(f, nullptr, out, sizeof(out), 4096);
  TEST_ASSERT_EQUAL_UINT32(321, n);
  TEST_ASSERT_TRUE(n <= lamp_protocol::CONTROL_MAX_PAYLOAD);
}

// The real emitter path: worst-case fields built at the production cap
// (CONTROL_MAX_PAYLOAD), fed straight into buildControlOp. Every field
// survives and the frame builds.
void test_worst_case_fits_control_op_at_production_cap() {
  char json[1024];
  size_t jsonLen = build(worstCase(), worstConfig(), json, sizeof(json),
                         lamp_protocol::CONTROL_MAX_PAYLOAD);
  TEST_ASSERT_EQUAL_UINT32(408, jsonLen);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, json));
  TEST_ASSERT_EQUAL_STRING("secondary", d["oscRole"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(65535, d["px"].as<int>());
  TEST_ASSERT_TRUE(d["wifiChannelMismatch"].as<bool>());
  TEST_ASSERT_EQUAL_INT(11, d["wifiApChannel"].as<int>());
  TEST_ASSERT_EQUAL_STRING("255.255.255.255", d["ethIp"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(99, d["brightness"].as<int>());

  static const uint8_t kTarget[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  static const uint8_t kSrc[6]    = {0xDE,0xAD,0xBE,0xEF,0x00,0x01};
  uint8_t frame[lamp_protocol::CONTROL_MAX_SIZE];
  size_t frameLen = lamp_protocol::buildControlOp(
      frame, sizeof(frame), /*seq=*/1, kTarget, kSrc,
      reinterpret_cast<const uint8_t*>(json), jsonLen);
  TEST_ASSERT_TRUE(frameLen > 0);
}

// wispConfig carries every field at every value, defaults included, so an
// absent key never stands for a default on the app.
void test_config_carries_every_field_at_defaults() {
  char out[256];
  size_t n = wisp::buildWispConfigJson(wisp::WispConfigFields{}, out,
                                       sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("wispConfig", d["char"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("GRB", d["ledType"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(30, d["px"].as<int>());
  TEST_ASSERT_EQUAL_INT(100, d["brightness"].as<int>());
  TEST_ASSERT_EQUAL_UINT32(120000, d["driftIntervalMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(50, d["driftFadePct"].as<int>());
  TEST_ASSERT_EQUAL_INT(0, d["shuffleSeed"].as<int>());
  TEST_ASSERT_EQUAL_UINT32(0, d["opSeq"].as<uint32_t>());
  TEST_ASSERT_EQUAL_size_t(8, d.as<JsonObject>().size());
}

// The widest wispConfig: 148 B, inside the lamp's 256 B ingest slot and the
// emitter's buffer, a third of CONTROL_MAX_PAYLOAD.
void test_config_worst_case_pinned() {
  char out[256];
  wisp::WispConfigFields c = worstConfig();
  c.brightness = 100;
  size_t n = wisp::buildWispConfigJson(c, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT32(148, n);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("GRBW", d["ledType"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(4294967295u, d["opSeq"].as<uint32_t>());
}

// A buffer too small for the document yields 0, never a truncated frame.
void test_config_too_small_buffer_returns_zero() {
  char out[64];
  TEST_ASSERT_EQUAL_UINT32(
      0, wisp::buildWispConfigJson(worstConfig(), out, sizeof(out)));
}

// The lamp routes these broadcasts by their leading bytes
// (peekWispBroadcast), so "char" must serialize first in both.
void test_builders_write_char_first() {
  char out[512];
  size_t n = build(worstCase(), worstConfig(), out, sizeof(out),
                   lamp_protocol::CONTROL_MAX_PAYLOAD);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_INT(0, std::strncmp(out, "{\"char\":\"wispStatus\"", 20));
  n = wisp::buildWispConfigJson(worstConfig(), out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_INT(0, std::strncmp(out, "{\"char\":\"wispConfig\"", 20));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_core_worst_case_pinned_under_cap);
  RUN_TEST(test_source_live_always_present);
  RUN_TEST(test_last_seen_ms_keeps_growing_past_the_millis_wrap);
  RUN_TEST(test_worst_case_untruncated_length);
  RUN_TEST(test_status_only_worst_case_pinned);
  RUN_TEST(test_worst_case_fits_control_op_at_production_cap);
  RUN_TEST(test_brightness_omitted_at_default_emitted_when_dimmed);
  RUN_TEST(test_fully_loaded_worst_case_never_fails);
  RUN_TEST(test_offcolor_and_prefix_outrank_cosmetics);
  RUN_TEST(test_osc_role_outranks_cosmetics);
  RUN_TEST(test_live_fields_outrank_config_copy);
  RUN_TEST(test_osc_role_omitted_at_default_and_emitted_in_every_mode);
  RUN_TEST(test_defaults_omitted);
  RUN_TEST(test_non_default_identity_fields_emitted);
  RUN_TEST(test_op_seq_emitted);
  RUN_TEST(test_eth_ip_emitted_only_when_set);
  RUN_TEST(test_non_default_palette_and_drift_emitted);
  RUN_TEST(test_off_mode_emits_offcolor_and_drift);
  RUN_TEST(test_no_config_copy_carries_no_config_field);
  RUN_TEST(test_config_carries_every_field_at_defaults);
  RUN_TEST(test_config_worst_case_pinned);
  RUN_TEST(test_config_too_small_buffer_returns_zero);
  RUN_TEST(test_builders_write_char_first);
  return UNITY_END();
}
