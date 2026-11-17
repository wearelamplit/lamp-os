#include <unity.h>
#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>
#include "status/status_json.hpp"
#include "esp_timer.h"
#include "wire/lamp_protocol.hpp"

constexpr size_t CAP = 230;

// The guaranteed core at worst-case field widths. Every other field sheds
// before it, so a cap below it still yields this frame.
constexpr size_t CORE_PIN = 188;

// The widest lastSeenMs a wisp can send: esp_timer_get_time() / 1000 at the
// top of its int64 range.
constexpr uint64_t kWidestStampMs = INT64_MAX / 1000;

static void setUptimeMs(uint64_t ms) {
  g_mock_uptime_us = static_cast<int64_t>(ms * 1000);
}

void setUp() { setUptimeMs(1000); }
void tearDown() {}

// Every live field at its widest non-default value, Manual mode (widest
// source with oscRole), both mismatch fields and the widest Ethernet address.
// Sets the clock to the widest lastSeenMs.
static wisp::WispStatusFields worstCase() {
  setUptimeMs(kWidestStampMs);
  return wisp::WispStatusFields{
      false, false, "abcdef12", "manual", "secondary",
      255, 255, 255, 255, true,
      /*name=*/"12345678901234567890", /*hasPassword=*/true,
      /*wifiChannelMismatch=*/true, /*wifiApChannel=*/11,
      /*ethIp=*/"255.255.255.255", /*manualId=*/"abcdef12",
      /*manualSeq=*/65535 };
}

// Defaults everywhere, Manual mode.
static wisp::WispStatusFields plain() {
  return wisp::WispStatusFields{
      false, false, "", "manual", "primary",
      0, 0, 0, 0, false,
      /*name=*/"", /*hasPassword=*/false };
}

static size_t build(const wisp::WispStatusFields& f, char* out, size_t outCap,
                    size_t cap) {
  return wisp::buildWispStatusJson(f, out, outCap, cap);
}

// Every config field at its widest non-default value.
static wisp::WispConfigFields worstConfig() {
  wisp::WispConfigFields c;
  c.ledType = "GRBW";
  c.pixelCount = 65535;
  c.brightness = 99;
  c.driftIntervalMs = 4294967295u;  // begin() loads it from NVS unclamped
  c.driftFadePct = 100;
  c.shuffleSeed = 255;
  c.opSeq = 4294967295u;
  std::strcpy(c.ip, "255.255.255.255");
  c.swatches = false;  // "false" is wider than "true"
  return c;
}

// Guaranteed core at pathological widths: 16-digit lastSeenMs, widest
// source, both bools false (wider than true), hasPassword present, a
// non-empty manualId, a 5-digit manualSeq. All optional fields at their app
// defaults (omitted).
void test_core_worst_case_pinned_under_cap() {
  wisp::WispStatusFields f = plain();
  setUptimeMs(kWidestStampMs);
  f.hasPassword = true;
  f.manualId = "abcdef12";
  f.manualSeq = 65535;
  char out[256];
  size_t n = build(f, out, sizeof(out), CAP);
  TEST_ASSERT_EQUAL_UINT32(CORE_PIN, n);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("wispStatus", d["char"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("manual", d["source"].as<const char*>());
  TEST_ASSERT_FALSE(d["wifiConnected"].isNull());
  TEST_ASSERT_FALSE(d["sourceLive"].isNull());
  TEST_ASSERT_FALSE(d["lastSeenMs"].isNull());
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
  TEST_ASSERT_TRUE(d["configSplit"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["manualId"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(65535, d["manualSeq"].as<uint32_t>());
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

  setUptimeMs(kWrap - 1000);
  TEST_ASSERT_TRUE(build(f, out, sizeof(out), CAP) > 0);
  JsonDocument before;
  TEST_ASSERT_FALSE(deserializeJson(before, out));

  setUptimeMs(kWrap + 1000);
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
  size_t n = build(worstCase(), out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["source"].isNull());
  TEST_ASSERT_FALSE(d["sourceLive"].isNull());
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
  TEST_ASSERT_TRUE(d["configSplit"].as<bool>());
}

// Priority: offColor, oscRole and paletteIdPrefix lead the live fields. The
// cap fits exactly those three, and a short name would fit in place of any
// one of them, so a name ranked above one drops it.
void test_offcolor_role_and_prefix_outrank_the_rest() {
  wisp::WispStatusFields f = worstCase();
  f.source = "off";
  f.oscRole = "fill";
  f.name = "abc";
  char out[512];
  const size_t cap = 253;  // Off core 185 + offColor 22 + oscRole 17 + prefix 29
  size_t n = build(f, out, sizeof(out), cap);
  TEST_ASSERT_EQUAL_UINT32(cap, n);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("ffffffff", d["offColor"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("fill", d["oscRole"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["paletteIdPrefix"].as<const char*>());
  TEST_ASSERT_TRUE(d["name"].isNull());
  TEST_ASSERT_TRUE(d["ethIp"].isNull());
}

// The same in Manual, which carries no offColor.
void test_osc_role_and_prefix_outrank_the_rest() {
  wisp::WispStatusFields f = worstCase();
  f.name = "abc";
  char out[512];
  const size_t cap = 239;  // core 188 + oscRole 22 + paletteIdPrefix 29
  size_t n = build(f, out, sizeof(out), cap);
  TEST_ASSERT_EQUAL_UINT32(cap, n);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("secondary", d["oscRole"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["paletteIdPrefix"].as<const char*>());
  TEST_ASSERT_TRUE(d["name"].isNull());
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

// Non-default identity fields ride the wire when the frame has room.
void test_non_default_identity_fields_emitted() {
  wisp::WispStatusFields f = plain();
  f.name = "lamp-room";
  f.hasPassword = true;
  char out[256];
  size_t n = build(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("lamp-room", d["name"].as<const char*>());
}

// A non-default palette id rides the wire when the frame has room.
void test_non_default_palette_emitted() {
  wisp::WispStatusFields f = plain();
  f.paletteIdPrefix = "abcdef12";
  char out[256];
  size_t n = build(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["paletteIdPrefix"].as<const char*>());
}

// Off mode emits offColor.
void test_off_mode_emits_offcolor() {
  wisp::WispStatusFields f = plain();
  f.source = "off";
  f.offR = 200; f.offG = 100; f.offB = 50; f.offW = 25;
  f.hasOffColor = true;
  char out[256];
  size_t n = build(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("c8643219", d["offColor"].as<const char*>());
}

// Every status says it carries no settings, and carries none. The app reads
// them only from wispConfig.
void test_every_status_is_config_split() {
  char out[1024];
  const wisp::WispStatusFields statuses[] = {plain(), worstCase()};
  for (const auto& f : statuses) {
    size_t n = build(f, out, sizeof(out), lamp_protocol::CONTROL_MAX_PAYLOAD);
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, out));
    TEST_ASSERT_TRUE(d["configSplit"].as<bool>());
    const char* configKeys[] = {"ledType", "px", "brightness", "driftIntervalMs",
                                "driftFadePct", "shuffleSeed", "opSeq"};
    for (const char* k : configKeys) TEST_ASSERT_TRUE(d[k].isNull());
  }
}

// configSplit rides the core, so no cap sheds it. The app would read a status
// without it as carrying the settings at their defaults.
void test_config_split_survives_any_cap() {
  char out[256];
  size_t n = build(worstCase(), out, sizeof(out), /*cap=*/0);
  TEST_ASSERT_TRUE(n > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["configSplit"].as<bool>());
  TEST_ASSERT_TRUE(d["name"].isNull());
}

// manualId rides the core in every source mode, "" for an empty Manual
// palette, so the app reads its absence as an older wisp and never as empty.
void test_manual_id_rides_the_core_in_every_mode() {
  const char* sources[] = {"off", "manual", "osc"};
  const char* ids[] = {"", "abcdef12"};
  char out[256];
  for (const char* src : sources) {
    for (const char* id : ids) {
      wisp::WispStatusFields f = worstCase();
      f.source = src;
      f.manualId = id;
      TEST_ASSERT_TRUE(build(f, out, sizeof(out), /*cap=*/0) > 0);
      JsonDocument d;
      TEST_ASSERT_FALSE(deserializeJson(d, out));
      TEST_ASSERT_TRUE(d["manualId"].is<const char*>());
      TEST_ASSERT_EQUAL_STRING(id, d["manualId"].as<const char*>());
    }
  }
}

// manualSeq rides the core when a save set it, so no cap sheds the echo a
// save waits for, and is omitted at 0, which means no echo.
void test_manual_seq_rides_the_core_and_is_omitted_at_zero() {
  char out[1024];
  wisp::WispStatusFields f = worstCase();
  f.manualSeq = 7;
  TEST_ASSERT_TRUE(build(f, out, sizeof(out), /*cap=*/0) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_UINT32(7, d["manualSeq"].as<uint32_t>());

  f.manualSeq = 0;
  TEST_ASSERT_TRUE(build(f, out, sizeof(out), /*cap=*/4096) > 0);
  JsonDocument d2;
  TEST_ASSERT_FALSE(deserializeJson(d2, out));
  TEST_ASSERT_TRUE(d2["manualSeq"].isNull());
}

// Absolute worst case, untruncated: Off mode (offColor outweighs the longer
// "manual"), every live field widest, both mismatch fields, ethIp. It fits
// CONTROL_MAX_PAYLOAD with room to spare, so at the production cap nothing
// sheds. Manual mode is 341. The lamp's merge adds up to 301 B on top
// (test_wisp_status_merge), which is why the app recovers an over-512 status
// through the wispstatus page section. ArduinoJson escapes '"', '\' and
// \b \f \n \r \t to two bytes and passes other control bytes raw, so a name
// of 20 quotes (escaped to 40) is the true ceiling.
void test_worst_case_untruncated_length() {
  char out[1024];
  wisp::WispStatusFields f = worstCase();
  f.source = "off";
  size_t n = build(f, out, sizeof(out), /*cap=*/4096);
  TEST_ASSERT_EQUAL_UINT32(360, n);
  n = build(worstCase(), out, sizeof(out), /*cap=*/4096);
  TEST_ASSERT_EQUAL_UINT32(341, n);

  f.name = "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"";
  n = build(f, out, sizeof(out), /*cap=*/4096);
  TEST_ASSERT_EQUAL_UINT32(380, n);
  TEST_ASSERT_TRUE(n <= lamp_protocol::CONTROL_MAX_PAYLOAD);
}

// The real emitter path: worst-case fields built at the production cap
// (CONTROL_MAX_PAYLOAD), fed straight into buildControlOp. Every live field
// survives and the frame builds.
void test_worst_case_fits_control_op_at_production_cap() {
  char json[1024];
  size_t jsonLen = build(worstCase(), json, sizeof(json),
                         lamp_protocol::CONTROL_MAX_PAYLOAD);
  TEST_ASSERT_EQUAL_UINT32(341, jsonLen);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, json));
  TEST_ASSERT_EQUAL_STRING("secondary", d["oscRole"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("abcdef12", d["paletteIdPrefix"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("12345678901234567890", d["name"].as<const char*>());
  TEST_ASSERT_TRUE(d["wifiChannelMismatch"].as<bool>());
  TEST_ASSERT_EQUAL_INT(11, d["wifiApChannel"].as<int>());
  TEST_ASSERT_EQUAL_STRING("255.255.255.255", d["ethIp"].as<const char*>());
  TEST_ASSERT_TRUE(d["configSplit"].as<bool>());

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
  TEST_ASSERT_TRUE(d["ip"].is<const char*>());
  TEST_ASSERT_EQUAL_STRING("", d["ip"].as<const char*>());
  TEST_ASSERT_TRUE(d["swatches"].is<bool>());
  TEST_ASSERT_FALSE(d["swatches"].as<bool>());
  TEST_ASSERT_EQUAL_size_t(10, d.as<JsonObject>().size());
}

// The widest wispConfig. It must stay at most 255 B, since the builder
// refuses 256 B and the lamp's 256 B ingest slot is the hard ceiling.
void test_config_worst_case_pinned() {
  char out[256];
  wisp::WispConfigFields c = worstConfig();
  c.brightness = 100;
  size_t n = wisp::buildWispConfigJson(c, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT32(191, n);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("GRBW", d["ledType"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(4294967295u, d["opSeq"].as<uint32_t>());
  TEST_ASSERT_EQUAL_STRING("255.255.255.255", d["ip"].as<const char*>());
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
  size_t n = build(worstCase(), out, sizeof(out),
                   lamp_protocol::CONTROL_MAX_PAYLOAD);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_INT(0, std::strncmp(out, "{\"char\":\"wispStatus\"", 20));
  n = wisp::buildWispConfigJson(worstConfig(), out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_INT(0, std::strncmp(out, "{\"char\":\"wispConfig\"", 20));
}

void test_config_reports_swatches_when_set() {
  char out[256];
  wisp::WispConfigFields c;
  c.swatches = true;
  TEST_ASSERT_TRUE(wisp::buildWispConfigJson(c, out, sizeof(out)) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["swatches"].as<bool>());
}

// The send-to address is the Ethernet lease first, then the STA address while
// the join holds, else empty.
void test_send_to_ip_prefers_ethernet_then_sta() {
  char out[16];
  wisp::pickOscSendToIp("10.0.0.5", true, "192.168.1.42", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", out);
  wisp::pickOscSendToIp("10.0.0.5", false, "", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", out);
  wisp::pickOscSendToIp("", true, "192.168.1.42", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("192.168.1.42", out);
  wisp::pickOscSendToIp("", false, "192.168.1.42", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("", out);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_core_worst_case_pinned_under_cap);
  RUN_TEST(test_source_live_always_present);
  RUN_TEST(test_last_seen_ms_keeps_growing_past_the_millis_wrap);
  RUN_TEST(test_worst_case_untruncated_length);
  RUN_TEST(test_worst_case_fits_control_op_at_production_cap);
  RUN_TEST(test_fully_loaded_worst_case_never_fails);
  RUN_TEST(test_offcolor_role_and_prefix_outrank_the_rest);
  RUN_TEST(test_osc_role_and_prefix_outrank_the_rest);
  RUN_TEST(test_osc_role_omitted_at_default_and_emitted_in_every_mode);
  RUN_TEST(test_defaults_omitted);
  RUN_TEST(test_non_default_identity_fields_emitted);
  RUN_TEST(test_eth_ip_emitted_only_when_set);
  RUN_TEST(test_non_default_palette_emitted);
  RUN_TEST(test_off_mode_emits_offcolor);
  RUN_TEST(test_every_status_is_config_split);
  RUN_TEST(test_config_split_survives_any_cap);
  RUN_TEST(test_manual_id_rides_the_core_in_every_mode);
  RUN_TEST(test_manual_seq_rides_the_core_and_is_omitted_at_zero);
  RUN_TEST(test_config_carries_every_field_at_defaults);
  RUN_TEST(test_config_worst_case_pinned);
  RUN_TEST(test_config_reports_swatches_when_set);
  RUN_TEST(test_send_to_ip_prefers_ethernet_then_sta);
  RUN_TEST(test_config_too_small_buffer_returns_zero);
  RUN_TEST(test_builders_write_char_first);
  return UNITY_END();
}
