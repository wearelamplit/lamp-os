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
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50 };
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
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50 };
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
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50 };
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
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50 };
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
                            /*driftIntervalMs=*/120000, /*driftFadePct=*/50 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);          // frame is always produced, never 0
  TEST_ASSERT_TRUE(n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_FALSE(d["source"].isNull());   // essential field preserved
}

void test_drift_fields_are_emitted() {
  // Production-shaped manual frame: hasOffColor=true, a few zones, realistic
  // field widths. Option A suppresses offColor (source != "off") so both drift
  // fields fit within CAP alongside offColor being absent.
  int zones[3] = {0, 3, 7};
  wisp::WispStatusFields f{ 3, "nvs", zones, 3,
                            false, false, "abcdef12", 1000u, "manual",
                            255, 150, 50, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/90000, /*driftFadePct=*/40 };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_UINT32(90000, d["driftIntervalMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT8(40, d["driftFadePct"].as<uint8_t>());
  TEST_ASSERT_TRUE(d["offColor"].isNull());             // off mode only
}

void test_off_mode_emits_offcolor_not_drift() {
  wisp::WispStatusFields f{ 3, "nvs", nullptr, 0,
                            false, false, "abcdef12", 1000u, "off",
                            200, 100, 50, true, /*shuffleSeed=*/0,
                            /*driftIntervalMs=*/90000, /*driftFadePct=*/40 };
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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_worst_case_stays_under_cap);
  RUN_TEST(test_empty_observed_fits);
  RUN_TEST(test_nonzero_shuffle_seed_is_emitted);
  RUN_TEST(test_zero_shuffle_seed_is_omitted);
  RUN_TEST(test_nonzero_seed_worst_case_does_not_fail);
  RUN_TEST(test_drift_fields_are_emitted);
  RUN_TEST(test_off_mode_emits_offcolor_not_drift);
  return UNITY_END();
}
