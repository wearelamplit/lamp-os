#include <unity.h>
#include <ArduinoJson.h>
#include "status/status_json.hpp"

constexpr size_t CAP = 230;

void setUp() {}
void tearDown() {}

void test_worst_case_stays_under_cap() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;   // pathological width
  // shuffleSeed=0 → field omitted, keeping base JSON within CAP even at
  // pathological int widths (currentZone/lastSeenMs at INT_MAX).
  // source="aurora" → offColor suppressed (Option A); drift fields carry instead.
  wisp::WispStatusFields f{ 2147483647, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "aurora",
                            255, 255, 255, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n <= CAP);                            // the guarantee
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));  // bool is true on error
  TEST_ASSERT_TRUE(d["observedZones"].as<JsonArray>().size() < 16); // cap engaged
  TEST_ASSERT_FALSE(d["source"].isNull());              // field preserved
  TEST_ASSERT_TRUE(d["offColor"].isNull());             // off mode only
}

void test_empty_observed_fits() {
  wisp::WispStatusFields f{ 3, "appOp", nullptr, 0,
                            false, false, "abcdef12", 1000u, "off",
                            10, 20, 30, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
}

// When shuffleSeed is non-zero it must appear in the serialized JSON.
// Small inputs ensure it fits within CAP; the pathological test above
// covers the cap guarantee for seed=0 (field omitted → nothing added).
void test_nonzero_shuffle_seed_is_emitted() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "manual",
                            10, 20, 30, true, /*shuffleSeed=*/42,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_INT(42, d["shuffleSeed"].as<int>());
}

// seed=0 must NOT appear in the serialized JSON (omit-when-zero contract).
void test_zero_shuffle_seed_is_omitted() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "manual",
                            10, 20, 30, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  JsonDocument d;
  deserializeJson(d, out);
  TEST_ASSERT_TRUE(d["shuffleSeed"].isNull());
}

// A non-zero shuffleSeed at pathological field widths must NOT fail the frame:
// the seed is dropped to fit so the frame is always produced (a failed frame
// stops the wisp broadcasting status + palette, vanishing it from the app) and
// essential fields survive.
void test_nonzero_seed_worst_case_does_not_fail() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;
  wisp::WispStatusFields f{ 2147483647, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "aurora",
                            255, 255, 255, true, /*shuffleSeed=*/255,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);          // frame is always produced, never 0
  TEST_ASSERT_TRUE(n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["source"].isNull());   // essential field preserved
}

// shuffleSeed is a priority field: drift fields and observedZones are dropped
// first, so the seed survives and the app's predictTuple() stays in sync.
// hasPassword (non-droppable) takes precedence over shuffleSeed in the
// absolute pathological frame, so use a frame where both fit.
void test_shuffle_seed_survives_worst_case() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;
  wisp::WispStatusFields f{ 7, "firstSeen", zones, 16,
                            true, true, "abcdef12", 60000u, "aurora",
                            255, 255, 255, true, /*shuffleSeed=*/255,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_INT(255, d["shuffleSeed"].as<int>());
}

// Off mode + moderate-uptime lastSeenMs + non-zero seed: seed drops to absorb
// budget pressure (offColor + hasPassword must both survive), but the frame
// is always produced. lastSeenMs=1000000 (7 digits) keeps fixed+offColor+
// hasPassword at exactly 230 B so zones/seed absorb any remaining slack.
void test_off_mode_longuptime_seed_still_produces_frame() {
  wisp::WispStatusFields f{ 15, "nvs", nullptr, 0,
                            true, true, "abcdef12", 1000000u, "off",
                            255, 255, 255, true, /*shuffleSeed=*/255,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);          // frame always produced
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["offColor"].isNull());    // off-mode essential kept
}

void test_drift_fields_are_emitted() {
  // Manual frame: hasPassword(non-droppable, ~18B) + driftIntervalMs(~24B)
  // consume most of the 230B budget, leaving driftFadePct to be dropped when
  // the fixed fields already fill the frame. driftIntervalMs is checked; it
  // is the higher-priority drift field (the app needs the interval to show the
  // slider position, even if it falls back to a default fade).
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "manual",
                            255, 150, 50, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/90000, /*driftFadePct=*/40,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_UINT32(90000, d["driftIntervalMs"].as<uint32_t>());
  TEST_ASSERT_TRUE(d["offColor"].isNull());             // off mode only
}

void test_off_mode_emits_offcolor_not_drift() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "off",
                            200, 100, 50, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/90000, /*driftFadePct=*/40,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["offColor"].isNull());            // emitted in off mode
  TEST_ASSERT_EQUAL_UINT8(200, d["offColor"][0].as<uint8_t>());
  TEST_ASSERT_TRUE(d["driftIntervalMs"].isNull());      // suppressed in off mode
  TEST_ASSERT_TRUE(d["driftFadePct"].isNull());
}

// name field is emitted when non-empty, omitted when empty.
void test_name_emitted_when_set() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "aurora",
                            0, 0, 0, false, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"lamp-room", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("lamp-room", d["name"].as<const char*>());
}

void test_name_omitted_when_empty() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "aurora",
                            0, 0, 0, false, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["name"].isNull());
}

// A 20-char name + hasPassword (non-droppable) fit within 230 B when other
// optional fields (drift, zones) are absent. Uses a shorter paletteIdPrefix
// and smaller lastSeenMs to stay within budget with hasPassword present.
void test_max_name_realistic_widths_preserved() {
  wisp::WispStatusFields f{ 7, "nvs", nullptr, 0,
                            true, true, "abcd", 1000u, "aurora",
                            255, 255, 255, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"12345678901234567890", /*hasPassword=*/false };  // exactly 20 chars
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_STRING("12345678901234567890", d["name"].as<const char*>());
}

// Frame is always produced even when a 20-char name + pathological widths
// exhaust the budget (name is a last-resort drop, same as shuffleSeed).
void test_max_name_pathological_frame_always_produced() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;
  wisp::WispStatusFields f{ 2147483647, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "aurora",
                            255, 255, 255, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"12345678901234567890", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);     // frame always produced, never 0
  TEST_ASSERT_TRUE(n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["source"].isNull());  // essential field preserved
}

// Use source="off" to avoid drift fields, keeping base JSON under 230 B so
// hasPassword fits without being dropped.
void test_has_password_false_emitted() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "off",
                            10, 20, 30, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/false };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["hasPassword"].isNull());
  TEST_ASSERT_FALSE(d["hasPassword"].as<bool>());
}

void test_has_password_true_emitted() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "off",
                            10, 20, 30, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"", /*hasPassword=*/true };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["hasPassword"].isNull());
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
}

// hasPassword must be present in a maximally-stuffed Aurora frame.
// The app defaulting a missing field to false would wrongly assume open access,
// so hasPassword is non-droppable (unlike drift/zones which absorb truncation).
void test_has_password_present_in_worst_case_aurora() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;
  wisp::WispStatusFields f{ 2147483647, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "aurora",
                            255, 255, 255, true, /*shuffleSeed=*/255,
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50,
                            /*name=*/"12345678901234567890", /*hasPassword=*/true };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["hasPassword"].isNull());
  TEST_ASSERT_TRUE(d["hasPassword"].as<bool>());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_worst_case_stays_under_cap);
  RUN_TEST(test_empty_observed_fits);
  RUN_TEST(test_nonzero_shuffle_seed_is_emitted);
  RUN_TEST(test_zero_shuffle_seed_is_omitted);
  RUN_TEST(test_nonzero_seed_worst_case_does_not_fail);
  RUN_TEST(test_shuffle_seed_survives_worst_case);
  RUN_TEST(test_off_mode_longuptime_seed_still_produces_frame);
  RUN_TEST(test_drift_fields_are_emitted);
  RUN_TEST(test_off_mode_emits_offcolor_not_drift);
  RUN_TEST(test_name_emitted_when_set);
  RUN_TEST(test_name_omitted_when_empty);
  RUN_TEST(test_max_name_realistic_widths_preserved);
  RUN_TEST(test_max_name_pathological_frame_always_produced);
  RUN_TEST(test_has_password_false_emitted);
  RUN_TEST(test_has_password_true_emitted);
  RUN_TEST(test_has_password_present_in_worst_case_aurora);
  return UNITY_END();
}
