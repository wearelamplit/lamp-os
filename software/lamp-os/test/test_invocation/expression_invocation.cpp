// Native-host tests for ExpressionInvocation.
//
// Pins:
//   1. cascade-key stripping + delayMs clamp.
//   2. serialize/parse round-trip, including the packed-hex colors wire form
//      ("rrggbbww" per color, no '#', no separators; key omitted when empty).
//   3. malformed colors (bad length / non-hex) drop whole; parse still succeeds.
//   4. worst-case 8-color zoned glitchy payload fits COMMAND_MAX_PAYLOAD.
//   5. the `hp` hop key: absent at hop 0, round trips, clamps on parse.

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
  lamp::ExpressionParams in = {
      {"cascadeEnabled", 1}, {"pulseSpeed", 5}};
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(1, (int)out.size());
  TEST_ASSERT_EQUAL(5, (int)out.get("pulseSpeed", 0));
  TEST_ASSERT_EQUAL(0, (int)out.contains("cascadeEnabled"));
}

void test_strip_removes_cascade_stagger_ms() {
  lamp::ExpressionParams in = {
      {"cascadeStaggerMs", 2000}, {"fadeDuration", 5000}};
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(1, (int)out.size());
  TEST_ASSERT_EQUAL(5000, (int)out.get("fadeDuration", 0));
  TEST_ASSERT_EQUAL(0, (int)out.contains("cascadeStaggerMs"));
}

void test_strip_removes_both_cascade_keys() {
  lamp::ExpressionParams in = {
      {"cascadeEnabled", 1},
      {"cascadeStaggerMs", 1000},
      {"durationMin", 1},
      {"durationMax", 3}};
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(2, (int)out.size());
  TEST_ASSERT_EQUAL(1, (int)out.get("durationMin", 0));
  TEST_ASSERT_EQUAL(3, (int)out.get("durationMax", 0));
}

void test_strip_is_noop_when_no_cascade_keys() {
  lamp::ExpressionParams in = {
      {"pulseSpeed", 5}, {"durationMin", 1}};
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(2, (int)out.size());
  TEST_ASSERT_EQUAL(5, (int)out.get("pulseSpeed", 0));
  TEST_ASSERT_EQUAL(1, (int)out.get("durationMin", 0));
}

void test_strip_handles_empty_map() {
  lamp::ExpressionParams in;
  auto out = lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(0, (int)out.size());
}

void test_strip_does_not_mutate_input() {
  lamp::ExpressionParams in = {
      {"cascadeEnabled", 1}, {"pulseSpeed", 5}};
  lamp::parametersWithoutCascadeKeys(in);
  TEST_ASSERT_EQUAL(2, (int)in.size());
  TEST_ASSERT_EQUAL(1, (int)in.get("cascadeEnabled", 0));
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

// --- hp hop key ---

static uint8_t parsedHops(const char* json) {
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
  lamp::ExpressionInvocation out;
  out.hops = 77;
  TEST_ASSERT_TRUE(lamp::parseInvocation(doc.as<JsonObjectConst>(), out));
  return out.hops;
}

// An origin frame is the same bytes a lamp that never heard of `hp` sends.
void test_origin_frame_carries_no_hp() {
  lamp::ExpressionInvocation in;
  in.type = "glitchy";
  in.target = 2;
  in.colors.push_back(lamp::Color(0xFF, 0x23, 0x42, 0x12));
  in.parameters.set("pulseSpeed", 5);
  std::string json;
  lamp::serializeInvocation(in, json);
  TEST_ASSERT_EQUAL_STRING(
      "{\"type\":\"glitchy\",\"target\":2,\"delayMs\":0,\"colors\":\"ff234212\","
      "\"parameters\":{\"pulseSpeed\":5}}",
      json.c_str());
}

void test_missing_hp_reads_as_hop_zero() {
  TEST_ASSERT_EQUAL_UINT8(0, parsedHops("{\"type\":\"glitchy\"}"));
}

void test_hops_round_trip() {
  for (uint8_t hops : {1, 2, 7, 255}) {
    lamp::ExpressionInvocation in;
    in.type = "glitchy";
    in.hops = hops;
    lamp::ExpressionInvocation out;
    std::string json;
    TEST_ASSERT_TRUE(roundTrip(in, out, json));
    TEST_ASSERT_EQUAL_UINT8(hops, out.hops);
  }
}

void test_hp_clamps_and_rejects_non_numbers() {
  TEST_ASSERT_EQUAL_UINT8(255, parsedHops("{\"type\":\"glitchy\",\"hp\":300}"));
  TEST_ASSERT_EQUAL_UINT8(255, parsedHops("{\"type\":\"glitchy\",\"hp\":4000000000}"));
  TEST_ASSERT_EQUAL_UINT8(0, parsedHops("{\"type\":\"glitchy\",\"hp\":-1}"));
  TEST_ASSERT_EQUAL_UINT8(0, parsedHops("{\"type\":\"glitchy\",\"hp\":\"2\"}"));
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

// --- peer input caps ---

// A peer's invocation is held to the expression caps, so a frame packed with
// colours or parameter keys cannot grow a transient past what the app writes.
void test_parse_caps_colors_and_params() {
  std::string json = "{\"type\":\"glitchy\",\"colors\":\"";
  for (int i = 0; i < 20; ++i) json += "ff234212";
  json += "\",\"parameters\":{";
  for (int i = 0; i < 30; ++i) {
    json += std::string(i ? ",\"" : "\"") +
            lamp::kExpressionParamKeys[i % lamp::kExpressionParamKeyCount] + "\":1";
  }
  json += "}}";
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
  lamp::ExpressionInvocation out;
  TEST_ASSERT_TRUE(lamp::parseInvocation(doc.as<JsonObjectConst>(), out));
  TEST_ASSERT_EQUAL_UINT(lamp::kMaxExpressionColors, out.colors.size());
  TEST_ASSERT_EQUAL_UINT(lamp::kMaxExpressionParams, out.parameters.size());
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
  RUN_TEST(test_origin_frame_carries_no_hp);
  RUN_TEST(test_missing_hp_reads_as_hop_zero);
  RUN_TEST(test_hops_round_trip);
  RUN_TEST(test_hp_clamps_and_rejects_non_numbers);
  RUN_TEST(test_parse_drops_truncated_colors);
  RUN_TEST(test_parse_drops_odd_length_colors);
  RUN_TEST(test_parse_drops_non_hex_colors);
  RUN_TEST(test_parse_caps_colors_and_params);
  RUN_TEST(test_zoned_glitchy_8_colors_fits_command_payload);
  return UNITY_END();
}
