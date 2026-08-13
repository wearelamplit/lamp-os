// Native-host tests for ExpressionInvocation.
//
// Pins:
//   1. cascade-key stripping + delayMs clamp.
//   2. serialize/parse round-trip, including the packed-hex colors wire form
//      ("rrggbbww" per color, no '#', no separators; key omitted when empty).
//   3. malformed colors (bad length / non-hex) drop whole; parse still succeeds.
//   4. worst-case 8-color zoned glitchy payload fits COMMAND_MAX_PAYLOAD.

#include <unity.h>

#include <cstdio>
#include <map>
#include <string>

#include "components/network/protocol/command.hpp"

// Native-test seam: include the .cpps for the real definitions.
#include "expressions/expression_invocation.cpp"
#include "util/color.cpp"

void setUp(void) {}
void tearDown(void) {}

// --- cascade-key stripping ---

void test_strip_removes_cascade_enabled() {
  std::map<std::string, uint32_t> in = {
      {"cascadeEnabled", 1}, {"pulseSpeed", 5}};
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(1, (int)out.size());
  TEST_ASSERT_EQUAL(5, (int)out["pulseSpeed"]);
  TEST_ASSERT_EQUAL(0, (int)out.count("cascadeEnabled"));
}

void test_strip_removes_cascade_stagger_ms() {
  std::map<std::string, uint32_t> in = {
      {"cascadeStaggerMs", 2000}, {"holdMs", 5000}};
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(1, (int)out.size());
  TEST_ASSERT_EQUAL(5000, (int)out["holdMs"]);
  TEST_ASSERT_EQUAL(0, (int)out.count("cascadeStaggerMs"));
}

void test_strip_removes_both_cascade_keys() {
  std::map<std::string, uint32_t> in = {
      {"cascadeEnabled", 1},
      {"cascadeStaggerMs", 1000},
      {"durationMin", 1},
      {"durationMax", 3}};
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(2, (int)out.size());
  TEST_ASSERT_EQUAL(1, (int)out["durationMin"]);
  TEST_ASSERT_EQUAL(3, (int)out["durationMax"]);
}

void test_strip_is_noop_when_no_cascade_keys() {
  std::map<std::string, uint32_t> in = {
      {"pulseSpeed", 5}, {"durationMin", 1}};
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(2, (int)out.size());
  TEST_ASSERT_EQUAL(5, (int)out["pulseSpeed"]);
  TEST_ASSERT_EQUAL(1, (int)out["durationMin"]);
}

void test_strip_handles_empty_map() {
  std::map<std::string, uint32_t> in;
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(0, (int)out.size());
}

void test_strip_does_not_mutate_input() {
  std::map<std::string, uint32_t> in = {
      {"cascadeEnabled", 1}, {"pulseSpeed", 5}};
  lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(2, (int)in.size());
  TEST_ASSERT_EQUAL(1, (int)in["cascadeEnabled"]);
}

// --- delayMs clamp ---

void test_clamp_delay_passes_under_limit() {
  TEST_ASSERT_EQUAL_UINT32(500u, lamp::clampDelayMs(500u));
}

void test_clamp_delay_caps_at_ceiling() {
  TEST_ASSERT_EQUAL_UINT32(lamp::kMaxDelayMs, lamp::clampDelayMs(60000u));
}

void test_clamp_delay_handles_max_uint32() {
  TEST_ASSERT_EQUAL_UINT32(lamp::kMaxDelayMs, lamp::clampDelayMs(0xFFFFFFFFu));
}

void test_clamp_delay_passes_zero() {
  TEST_ASSERT_EQUAL_UINT32(0u, lamp::clampDelayMs(0u));
}

// --- serialize / parse round-trip ---

static bool roundTrip(const lamp::ExpressionInvocation& in,
                      lamp::ExpressionInvocation& out, std::string& json) {
  lamp::serializeInvocation(in, json);
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  return lamp::parseInvocation(doc.as<JsonObjectConst>(), out);
}

void test_round_trip_one_color() {
  lamp::ExpressionInvocation in;
  in.type = "glitchy";
  in.target = 1;
  in.delayMs = 250;
  in.colors.push_back(lamp::Color(0xFF, 0x23, 0x42, 0x12));

  lamp::ExpressionInvocation out;
  std::string json;
  TEST_ASSERT_TRUE(roundTrip(in, out, json));
  TEST_ASSERT_TRUE(json.find("\"colors\":\"ff234212\"") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("glitchy", out.type.c_str());
  TEST_ASSERT_EQUAL_UINT8(1, out.target);
  TEST_ASSERT_EQUAL_UINT32(250u, out.delayMs);
  TEST_ASSERT_EQUAL_UINT(1, out.colors.size());
  TEST_ASSERT_TRUE(in.colors[0] == out.colors[0]);
}

void test_round_trip_eight_colors() {
  lamp::ExpressionInvocation in;
  in.type = "glitchy";
  for (uint8_t i = 0; i < 8; i++) {
    in.colors.push_back(lamp::Color(i, 0x10 + i, 0xA0 + i, 0xF0 + i));
  }

  lamp::ExpressionInvocation out;
  std::string json;
  TEST_ASSERT_TRUE(roundTrip(in, out, json));
  TEST_ASSERT_EQUAL_UINT(8, out.colors.size());
  for (size_t i = 0; i < 8; i++) {
    TEST_ASSERT_TRUE(in.colors[i] == out.colors[i]);
  }
}

void test_serialize_omits_colors_when_empty() {
  lamp::ExpressionInvocation in;
  in.type = "pulse";

  std::string json;
  lamp::serializeInvocation(in, json);
  TEST_ASSERT_TRUE(json.find("colors") == std::string::npos);

  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
  lamp::ExpressionInvocation out;
  TEST_ASSERT_TRUE(lamp::parseInvocation(doc.as<JsonObjectConst>(), out));
  TEST_ASSERT_EQUAL_UINT(0, out.colors.size());
}

// --- malformed colors: dropped whole, invocation still parses ---

static void assertColorsDropped(const char* json) {
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
  lamp::ExpressionInvocation out;
  TEST_ASSERT_TRUE(lamp::parseInvocation(doc.as<JsonObjectConst>(), out));
  TEST_ASSERT_EQUAL_UINT(0, out.colors.size());
}

void test_parse_drops_truncated_colors() {
  assertColorsDropped("{\"type\":\"glitchy\",\"colors\":\"ff234212ff2342\"}");
}

void test_parse_drops_odd_length_colors() {
  assertColorsDropped("{\"type\":\"glitchy\",\"colors\":\"ff23421\"}");
}

void test_parse_drops_non_hex_colors() {
  assertColorsDropped("{\"type\":\"glitchy\",\"colors\":\"ff234212zz234212\"}");
}

// --- worst-case payload budget ---

// The 8-color zoned glitchy config that overflowed the command cap under the
// old "#rrggbbww"-array encoding (~255 B). Full app-written param set on the
// standard shade window (38 px). Values with more digits (maxed count/size
// sliders, 4-digit durations, staggered delayMs) can still exceed the cap;
// ExpressionManager drops those with the loud [cascade] ERR log.
void test_zoned_glitchy_8_colors_fits_command_payload() {
  lamp::ExpressionInvocation inv;
  inv.type = "glitchy";
  inv.target = 1;
  inv.delayMs = 0;
  for (int i = 0; i < 8; i++) {
    inv.colors.push_back(lamp::Color(0xFF, 0xFF, 0xFF, 0xFF));
  }
  inv.parameters = {
      {"durationMin", 30}, {"durationMax", 120}, {"fullStrip", 0},
      {"posMin", 0},       {"posMax", 37},       {"count", 1},
      {"size", 1},
  };

  std::string json;
  lamp::serializeInvocation(inv, json);
  std::printf("8-color zoned glitchy payload: %u bytes (cap %u)\n",
              (unsigned)json.size(),
              (unsigned)lamp_protocol::COMMAND_MAX_PAYLOAD);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(lamp_protocol::COMMAND_MAX_PAYLOAD,
                                 json.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_strip_removes_cascade_enabled);
  RUN_TEST(test_strip_removes_cascade_stagger_ms);
  RUN_TEST(test_strip_removes_both_cascade_keys);
  RUN_TEST(test_strip_is_noop_when_no_cascade_keys);
  RUN_TEST(test_strip_handles_empty_map);
  RUN_TEST(test_strip_does_not_mutate_input);
  RUN_TEST(test_clamp_delay_passes_under_limit);
  RUN_TEST(test_clamp_delay_caps_at_ceiling);
  RUN_TEST(test_clamp_delay_handles_max_uint32);
  RUN_TEST(test_clamp_delay_passes_zero);
  RUN_TEST(test_round_trip_one_color);
  RUN_TEST(test_round_trip_eight_colors);
  RUN_TEST(test_serialize_omits_colors_when_empty);
  RUN_TEST(test_parse_drops_truncated_colors);
  RUN_TEST(test_parse_drops_odd_length_colors);
  RUN_TEST(test_parse_drops_non_hex_colors);
  RUN_TEST(test_zoned_glitchy_8_colors_fits_command_payload);
  return UNITY_END();
}
