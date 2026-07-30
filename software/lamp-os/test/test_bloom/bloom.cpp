// Native-host pin for the bloom internal expression: the one-shot white-ward
// swell math (bloomSwell / bloomWhiten) and the serializeCatalog `internal`
// skip. The Expression state machine (playOnce -> STOPPED after `frames`) is
// covered by AnimatedBehavior; here we pin bloom's unique logic.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "expressions/bloom/bloom_expression.hpp"
#include "expressions/pulse/pulse_expression.hpp"

#include "../../src/expressions/expression_registry.cpp"

using namespace lamp;

void setUp() {}
void tearDown() {}

// --- swell shape: dark at both ends (self-terminating one-shot), peak middle -
void test_swell_starts_and_ends_dark() {
  TEST_ASSERT_EQUAL_UINT8(0, bloomSwell(0, kBloomFrames));
  TEST_ASSERT_EQUAL_UINT8(0, bloomSwell(kBloomFrames, kBloomFrames));
}

void test_swell_peaks_near_midpoint() {
  uint8_t peak = 0;
  uint32_t peakFrame = 0;
  for (uint32_t f = 0; f <= kBloomFrames; ++f) {
    uint8_t s = bloomSwell(f, kBloomFrames);
    if (s > peak) { peak = s; peakFrame = f; }
  }
  TEST_ASSERT_GREATER_THAN_UINT8(200, peak);  // reaches near-full white at the top
  // Peak lands around the middle of the animation.
  TEST_ASSERT_TRUE(peakFrame >= kBloomFrames / 2 - 3 && peakFrame <= kBloomFrames / 2 + 3);
}

void test_swell_degenerate_short_frames() {
  TEST_ASSERT_EQUAL_UINT8(0, bloomSwell(0, 1));  // no div-by-zero, no swell
}

// --- whiten: integer per-channel lerp toward 255 -----------------------------
void test_whiten_zero_is_identity() {
  Color c(100, 50, 20, 10);
  TEST_ASSERT_TRUE(bloomWhiten(c, 0) == c);
}

void test_whiten_full_is_white() {
  Color c(100, 50, 20, 10);
  Color w = bloomWhiten(c, 255);
  TEST_ASSERT_EQUAL_UINT8(255, w.r);
  TEST_ASSERT_EQUAL_UINT8(255, w.g);
  TEST_ASSERT_EQUAL_UINT8(255, w.b);
  TEST_ASSERT_EQUAL_UINT8(255, w.w);
}

void test_whiten_moves_channels_toward_white() {
  Color c(100, 50, 20, 0);
  Color w = bloomWhiten(c, 128);
  TEST_ASSERT_GREATER_THAN_UINT8(c.r, w.r);
  TEST_ASSERT_GREATER_THAN_UINT8(c.g, w.g);
  TEST_ASSERT_GREATER_THAN_UINT8(c.b, w.b);
  TEST_ASSERT_GREATER_THAN_UINT8(c.w, w.w);
}

// Composites additively over whatever's below: never darker than the base.
void test_whiten_is_additive_over_base() {
  const Color base(80, 120, 200, 30);
  for (int s = 0; s <= 255; s += 15) {
    Color out = bloomWhiten(base, static_cast<uint8_t>(s));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(base.r, out.r);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(base.g, out.g);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(base.b, out.b);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(base.w, out.w);
  }
}

// --- serializeCatalog skips internal descriptors -----------------------------
void test_internal_descriptor_skipped_by_catalog() {
  TEST_ASSERT_TRUE(kBloomDescriptorData.internal);
  TEST_ASSERT_FALSE(kPulseDescriptorData.internal);

  ExpressionRegistry reg;
  reg.add(kPulseDescriptorData);
  reg.add(kBloomDescriptorData);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());
  auto exprs = doc["expressions"].as<JsonArray>();

  bool sawPulse = false, sawBloom = false;
  for (JsonObject e : exprs) {
    std::string id = e["id"].as<const char*>();
    if (id == "pulse") sawPulse = true;
    if (id == "bloom") sawBloom = true;
  }
  TEST_ASSERT_EQUAL_size_t(1, exprs.size());
  TEST_ASSERT_TRUE(sawPulse);
  TEST_ASSERT_FALSE(sawBloom);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_swell_starts_and_ends_dark);
  RUN_TEST(test_swell_peaks_near_midpoint);
  RUN_TEST(test_swell_degenerate_short_frames);
  RUN_TEST(test_whiten_zero_is_identity);
  RUN_TEST(test_whiten_full_is_white);
  RUN_TEST(test_whiten_moves_channels_toward_white);
  RUN_TEST(test_whiten_is_additive_over_base);
  RUN_TEST(test_internal_descriptor_skipped_by_catalog);
  return UNITY_END();
}
