// Pins the exprcat wire JSON for the five production descriptors. Registers
// the real header-defined descriptor data (make-less; .make needs Arduino),
// so any production descriptor change fails here instead of drifting.
#include <unity.h>
#include <ArduinoJson.h>

#include "expressions/breathing/breathing_expression.hpp"
#include "expressions/glitchy/glitchy_expression.hpp"
#include "expressions/pulse/pulse_expression.hpp"
#include "expressions/shifty/shifty_expression.hpp"
#include "expressions/spotty/spotty_expression.hpp"

#include "../../src/expressions/expression_registry.cpp"

using namespace lamp;

// ---- Test helpers ----------------------------------------------------------

static ExpressionRegistry g_reg;
static JsonDocument g_doc;

void setUp() {
  g_reg = ExpressionRegistry{};
  g_reg.add(kGlitchyDescriptorData);
  g_reg.add(kPulseDescriptorData);
  g_reg.add(kBreathingDescriptorData);
  g_reg.add(kShiftyDescriptorData);
  g_reg.add(kSpottyDescriptorData);
  g_doc.clear();
  deserializeJson(g_doc, g_reg.serializeCatalog());
}

void tearDown() {}

static JsonObject findById(const char* id) {
  for (JsonObject e : g_doc["expressions"].as<JsonArray>()) {
    if (std::string(e["id"].as<const char*>()) == id) return e;
  }
  return JsonObject{};
}

// ---- Tests -----------------------------------------------------------------

void test_all_five_ids_present() {
  const auto exprs = g_doc["expressions"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(5, exprs.size());
  TEST_ASSERT_NOT_NULL(findById("glitchy")["id"].as<const char*>());
  TEST_ASSERT_NOT_NULL(findById("pulse")["id"].as<const char*>());
  TEST_ASSERT_NOT_NULL(findById("breathing")["id"].as<const char*>());
  TEST_ASSERT_NOT_NULL(findById("shifty")["id"].as<const char*>());
  TEST_ASSERT_NOT_NULL(findById("spotty")["id"].as<const char*>());
}

void test_continuous_flags() {
  TEST_ASSERT_TRUE(findById("breathing")["continuous"].as<bool>());
  TEST_ASSERT_TRUE(findById("shifty")["continuous"].as<bool>());
  TEST_ASSERT_TRUE(findById("spotty")["continuous"].as<bool>());
  TEST_ASSERT_FALSE(findById("glitchy")["continuous"].as<bool>());
  TEST_ASSERT_FALSE(findById("pulse")["continuous"].as<bool>());
}

void test_pauses_wisp_override_flags() {
  TEST_ASSERT_TRUE(findById("breathing")["pausesWispOverride"].as<bool>());
  TEST_ASSERT_TRUE(findById("shifty")["pausesWispOverride"].as<bool>());
  TEST_ASSERT_TRUE(findById("spotty")["pausesWispOverride"].as<bool>());
  TEST_ASSERT_TRUE(findById("glitchy")["pausesWispOverride"].isNull());
  TEST_ASSERT_TRUE(findById("pulse")["pausesWispOverride"].isNull());
}

void test_glitchy_zone_optional() {
  auto zone = findById("glitchy")["zone"];
  TEST_ASSERT_TRUE(zone.is<JsonObject>());
  TEST_ASSERT_TRUE(zone["optional"].as<bool>());
}

void test_glitchy_scatter_always_active_literal_max() {
  JsonObject e = findById("glitchy");
  JsonObject scatter;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "scatter") { scatter = p; break; }
  }
  TEST_ASSERT_FALSE(scatter.isNull());
  TEST_ASSERT_TRUE(scatter["requiresZoning"].isNull());
  auto maxVal = scatter["max"];
  TEST_ASSERT_FALSE(maxVal.is<JsonObject>());
  TEST_ASSERT_EQUAL_INT(5, maxVal.as<int>());
  TEST_ASSERT_EQUAL_INT(0, scatter["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(0, scatter["default"].as<int>());
  TEST_ASSERT_TRUE(scatter["unit"].isNull());
}

void test_glitchy_has_no_size_param() {
  JsonObject e = findById("glitchy");
  bool hasSize = false;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "size") hasSize = true;
  }
  TEST_ASSERT_FALSE(hasSize);
}

void test_glitchy_duration_range() {
  auto dur = findById("glitchy")["duration"];
  TEST_ASSERT_TRUE(dur.is<JsonObject>());
  TEST_ASSERT_EQUAL_INT(30,   dur["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(1000, dur["max"].as<int>());
  auto def = dur["default"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(2, def.size());
  TEST_ASSERT_EQUAL_INT(30,  def[0].as<int>());
  TEST_ASSERT_EQUAL_INT(120, def[1].as<int>());
  TEST_ASSERT_EQUAL_STRING("Each glitch lasts a random time in this range.",
                           dur["help"].as<const char*>());
}

void test_interval_help_present() {
  const char* expected = "A random time in this range is picked before each trigger.";
  TEST_ASSERT_EQUAL_STRING(expected, findById("glitchy")["interval"]["help"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(expected, findById("pulse")["interval"]["help"].as<const char*>());
}

void test_shifty_fillmode_enum_no_zoning() {
  JsonObject e = findById("shifty");
  JsonObject fillParam;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "fillMode") { fillParam = p; break; }
  }
  TEST_ASSERT_EQUAL_STRING("enum", fillParam["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(
      "How the new color spreads: Uniform all at once, Up/Down from one end, "
      "Bloom outward from the center.",
      fillParam["help"].as<const char*>());
  auto opts = fillParam["options"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(4, opts.size());
  // Zone is now toggle-driven, not enum-driven; no option carries zoning.
  for (JsonObject o : opts) TEST_ASSERT_TRUE(o["zoning"].isNull());
}

void test_shifty_fade_duration_range() {
  JsonObject e = findById("shifty");
  JsonObject fadeParam;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "fadeDuration") { fadeParam = p; break; }
  }
  TEST_ASSERT_EQUAL_INT(30,  fadeParam["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(600, fadeParam["max"].as<int>());
}

void test_shifty_zone_optional() {
  auto zone = findById("shifty")["zone"];
  TEST_ASSERT_TRUE(zone.is<JsonObject>());
  TEST_ASSERT_TRUE(zone["optional"].as<bool>());
}

void test_shifty_duration_hold_time() {
  auto dur = findById("shifty")["duration"];
  TEST_ASSERT_TRUE(dur.is<JsonObject>());
  TEST_ASSERT_EQUAL_INT(60,   dur["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(1800, dur["max"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Hold time", dur["label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(
      "How long each color holds before the next shift (a random time in this range).",
      dur["help"].as<const char*>());
  auto def = dur["default"].as<JsonArray>();
  TEST_ASSERT_EQUAL_INT(300, def[0].as<int>());
  TEST_ASSERT_EQUAL_INT(600, def[1].as<int>());
}

void test_breathing_has_no_interval() {
  TEST_ASSERT_TRUE(findById("breathing")["interval"].isNull());
}

void test_breathing_params_present() {
  JsonObject e = findById("breathing");
  bool hasBreathSpeed = false, hasSections = false;
  bool hasCount = false, hasSize = false, hasScatter = false;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    const char* k = p["key"].as<const char*>();
    if (std::string(k) == "breathSpeed") hasBreathSpeed = true;
    if (std::string(k) == "sections")    hasSections = true;
    if (std::string(k) == "count")       hasCount = true;
    if (std::string(k) == "size")        hasSize = true;
    if (std::string(k) == "scatter")     hasScatter = true;
  }
  TEST_ASSERT_TRUE(hasBreathSpeed);
  TEST_ASSERT_TRUE(hasSections);
  TEST_ASSERT_FALSE(hasCount);
  TEST_ASSERT_FALSE(hasSize);
  TEST_ASSERT_FALSE(hasScatter);
}

void test_breathing_speed_floor() {
  JsonObject e = findById("breathing");
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "breathSpeed") {
      TEST_ASSERT_EQUAL_INT(8, p["min"].as<int>());
      TEST_ASSERT_EQUAL_INT(60, p["max"].as<int>());
      TEST_ASSERT_EQUAL_INT(10, p["default"].as<int>());
      return;
    }
  }
  TEST_FAIL_MESSAGE("breathSpeed param not found");
}

void test_breathing_sections_range_always_active() {
  JsonObject e = findById("breathing");
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "sections") {
      TEST_ASSERT_EQUAL_STRING("Sections", p["label"].as<const char*>());
      TEST_ASSERT_EQUAL_INT(1, p["min"].as<int>());
      TEST_ASSERT_EQUAL_INT(5, p["max"].as<int>());
      TEST_ASSERT_EQUAL_INT(1, p["default"].as<int>());
      TEST_ASSERT_TRUE(p["requiresZoning"].isNull());
      return;
    }
  }
  TEST_FAIL_MESSAGE("sections param not found");
}

void test_spotty_speed_param() {
  JsonObject e = findById("spotty");
  bool found = false;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "spotSpeed") {
      found = true;
      TEST_ASSERT_EQUAL_STRING("int", p["type"].as<const char*>());
      TEST_ASSERT_EQUAL_INT(1,  p["min"].as<int>());
      TEST_ASSERT_EQUAL_INT(10, p["max"].as<int>());
      TEST_ASSERT_EQUAL_INT(3,  p["default"].as<int>());
      TEST_ASSERT_TRUE(p["invert"].as<bool>());
      TEST_ASSERT_EQUAL_STRING("stars", p["leftLabel"].as<const char*>());
      TEST_ASSERT_EQUAL_STRING("fire", p["rightLabel"].as<const char*>());
      TEST_ASSERT_EQUAL_STRING(
          "Stars: slow, gentle, unpredictable fades. Fire: fast flickers mixed with slower flames.",
          p["help"].as<const char*>());
      break;
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_spotty_has_no_interval() {
  TEST_ASSERT_TRUE(findById("spotty")["interval"].isNull());
}

void test_spotty_count_and_size_caps() {
  JsonObject e = findById("spotty");
  bool foundCount = false, foundSize = false;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    const std::string k = p["key"].as<const char*>();
    if (k == "count") {
      foundCount = true;
      TEST_ASSERT_EQUAL_STRING("pixels", p["max"]["rel"].as<const char*>());
      TEST_ASSERT_EQUAL_INT(5, p["max"]["cap"].as<int>());
      TEST_ASSERT_EQUAL_INT(3, p["default"].as<int>());
    } else if (k == "size") {
      foundSize = true;
      TEST_ASSERT_EQUAL_STRING("pixels", p["max"]["rel"].as<const char*>());
      TEST_ASSERT_EQUAL_INT(6, p["max"]["cap"].as<int>());
      TEST_ASSERT_EQUAL_INT(1, p["min"].as<int>());
      TEST_ASSERT_EQUAL_INT(3, p["default"].as<int>());
      TEST_ASSERT_EQUAL_STRING("Small", p["leftLabel"].as<const char*>());
      TEST_ASSERT_EQUAL_STRING("Large", p["rightLabel"].as<const char*>());
    }
  }
  TEST_ASSERT_TRUE(foundCount);
  TEST_ASSERT_TRUE(foundSize);
}

void test_pulse_zone_optional() {
  JsonObject e = findById("pulse");
  auto zone = e["zone"];
  TEST_ASSERT_TRUE(zone.is<JsonObject>());
  TEST_ASSERT_TRUE(zone["optional"].as<bool>());
}

void test_pulse_size_is_percent() {
  JsonObject e = findById("pulse");
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "size") {
      TEST_ASSERT_EQUAL_STRING("%", p["unit"].as<const char*>());
      TEST_ASSERT_EQUAL_INT(5, p["min"].as<int>());
      auto maxVal = p["max"];
      TEST_ASSERT_FALSE(maxVal.is<JsonObject>());
      TEST_ASSERT_EQUAL_INT(100, maxVal.as<int>());
      TEST_ASSERT_EQUAL_INT(40, p["default"].as<int>());
      return;
    }
  }
  TEST_FAIL_MESSAGE("size param not found");
}

void test_spotty_zone_optional() {
  auto zone = findById("spotty")["zone"];
  TEST_ASSERT_TRUE(zone.is<JsonObject>());
  TEST_ASSERT_TRUE(zone["optional"].as<bool>());
}

void test_breathing_zone_optional() {
  auto zone = findById("breathing")["zone"];
  TEST_ASSERT_TRUE(zone.is<JsonObject>());
  TEST_ASSERT_TRUE(zone["optional"].as<bool>());
}

void test_pulse_speed_invert_and_labels() {
  JsonObject e = findById("pulse");
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "pulseSpeed") {
      TEST_ASSERT_TRUE(p["invert"].as<bool>());
      TEST_ASSERT_EQUAL_STRING("slow", p["leftLabel"].as<const char*>());
      TEST_ASSERT_EQUAL_STRING("fast", p["rightLabel"].as<const char*>());
      return;
    }
  }
  TEST_FAIL_MESSAGE("pulseSpeed param not found");
}

void test_pulse_easing_param() {
  JsonObject e = findById("pulse");
  JsonObject easing;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "easing") { easing = p; break; }
  }
  TEST_ASSERT_FALSE(easing.isNull());
  TEST_ASSERT_EQUAL_STRING("enum", easing["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Motion", easing["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, easing["default"].as<int>());  // Linear reproduces today's sweep
  auto opts = easing["options"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(5, opts.size());
  TEST_ASSERT_EQUAL_STRING("Linear", opts[0]["label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Swell", opts[4]["label"].as<const char*>());
}

static JsonObject findParam(JsonObject e, const char* key) {
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == key) return p;
  }
  return JsonObject{};
}

void test_spotty_easing_param_defaults_linear() {
  JsonObject easing = findParam(findById("spotty"), "easing");
  TEST_ASSERT_FALSE(easing.isNull());
  TEST_ASSERT_EQUAL_STRING("enum", easing["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Motion", easing["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, easing["default"].as<int>());  // Linear reproduces the plain fade ramp
  TEST_ASSERT_EQUAL_size_t(5, easing["options"].as<JsonArray>().size());
}

void test_shifty_easing_param_defaults_linear() {
  JsonObject easing = findParam(findById("shifty"), "easing");
  TEST_ASSERT_FALSE(easing.isNull());
  TEST_ASSERT_EQUAL_STRING("enum", easing["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Motion", easing["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, easing["default"].as<int>());  // Linear reproduces today's linear fade
  TEST_ASSERT_EQUAL_size_t(5, easing["options"].as<JsonArray>().size());
}

void test_breathing_easing_param_defaults_smooth() {
  JsonObject easing = findParam(findById("breathing"), "easing");
  TEST_ASSERT_FALSE(easing.isNull());
  TEST_ASSERT_EQUAL_STRING("enum", easing["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Motion", easing["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(1, easing["default"].as<int>());  // Smooth approximates the sine breath curve
  TEST_ASSERT_EQUAL_size_t(5, easing["options"].as<JsonArray>().size());
}

void test_pulse_loop_param() {
  JsonObject e = findById("pulse");
  JsonObject loop;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "loop") { loop = p; break; }
  }
  TEST_ASSERT_FALSE(loop.isNull());
  TEST_ASSERT_EQUAL_STRING("enum", loop["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Loop", loop["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, loop["default"].as<int>());  // Trigger keeps single-sweep default
  auto opts = loop["options"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(2, opts.size());
  TEST_ASSERT_EQUAL_STRING("Trigger", opts[0]["label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Continuous", opts[1]["label"].as<const char*>());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_all_five_ids_present);
  RUN_TEST(test_continuous_flags);
  RUN_TEST(test_pauses_wisp_override_flags);
  RUN_TEST(test_glitchy_zone_optional);
  RUN_TEST(test_glitchy_scatter_always_active_literal_max);
  RUN_TEST(test_glitchy_has_no_size_param);
  RUN_TEST(test_glitchy_duration_range);
  RUN_TEST(test_interval_help_present);
  RUN_TEST(test_shifty_fillmode_enum_no_zoning);
  RUN_TEST(test_shifty_fade_duration_range);
  RUN_TEST(test_shifty_zone_optional);
  RUN_TEST(test_shifty_duration_hold_time);
  RUN_TEST(test_breathing_has_no_interval);
  RUN_TEST(test_breathing_params_present);
  RUN_TEST(test_breathing_speed_floor);
  RUN_TEST(test_breathing_sections_range_always_active);
  RUN_TEST(test_spotty_speed_param);
  RUN_TEST(test_spotty_has_no_interval);
  RUN_TEST(test_spotty_count_and_size_caps);
  RUN_TEST(test_pulse_zone_optional);
  RUN_TEST(test_pulse_size_is_percent);
  RUN_TEST(test_spotty_zone_optional);
  RUN_TEST(test_breathing_zone_optional);
  RUN_TEST(test_pulse_speed_invert_and_labels);
  RUN_TEST(test_pulse_easing_param);
  RUN_TEST(test_spotty_easing_param_defaults_linear);
  RUN_TEST(test_shifty_easing_param_defaults_linear);
  RUN_TEST(test_breathing_easing_param_defaults_smooth);
  RUN_TEST(test_pulse_loop_param);
  return UNITY_END();
}
