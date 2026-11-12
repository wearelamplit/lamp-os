// Pins the exprcat wire JSON for the five production descriptors. Registers
// the real header-defined descriptor data (make-less; .make needs Arduino),
// so any production descriptor change fails here instead of drifting.
#include <unity.h>
#include <ArduinoJson.h>

#include <cstring>
#include <vector>

#include "expressions/breathing/breathing_expression.hpp"
#include "expressions/shimmer/shimmer_expression.hpp"
#include "expressions/glitchy/glitchy_expression.hpp"
#include "expressions/pulse/pulse_expression.hpp"
#include "expressions/shifty/shifty_expression.hpp"
#include "expressions/spotty/spotty_expression.hpp"
#include "expressions/bloom/bloom_expression.hpp"
#include "expressions/yawn/yawn_expression.hpp"
#include "expressions/expr_variant_sets.hpp"
#include "config/config_types.hpp"
#include "expressions/expression_invocation.hpp"

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
  g_reg.add(kShimmerDescriptorData);
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
  TEST_ASSERT_EQUAL_size_t(6, exprs.size());
  TEST_ASSERT_NOT_NULL(findById("glitchy")["id"].as<const char*>());
  TEST_ASSERT_NOT_NULL(findById("pulse")["id"].as<const char*>());
  TEST_ASSERT_NOT_NULL(findById("breathing")["id"].as<const char*>());
  TEST_ASSERT_NOT_NULL(findById("shifty")["id"].as<const char*>());
  TEST_ASSERT_NOT_NULL(findById("spotty")["id"].as<const char*>());
  TEST_ASSERT_NOT_NULL(findById("flicker")["id"].as<const char*>());
}

void test_continuous_flags() {
  TEST_ASSERT_TRUE(findById("breathing")["continuous"].as<bool>());
  TEST_ASSERT_TRUE(findById("shifty")["continuous"].as<bool>());
  TEST_ASSERT_TRUE(findById("spotty")["continuous"].as<bool>());
  TEST_ASSERT_FALSE(findById("glitchy")["continuous"].as<bool>());
  TEST_ASSERT_FALSE(findById("pulse")["continuous"].as<bool>());
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

void test_glitchy_interval_bounds_and_min_gap() {
  auto iv = findById("glitchy")["interval"];
  TEST_ASSERT_TRUE(iv.is<JsonObject>());
  TEST_ASSERT_EQUAL_INT(300,   iv["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(18000, iv["max"].as<int>());
  TEST_ASSERT_EQUAL_INT(1800,  iv["minGap"].as<int>());
  auto def = iv["default"].as<JsonArray>();
  TEST_ASSERT_EQUAL_INT(1800, def[0].as<int>());
  TEST_ASSERT_EQUAL_INT(7200, def[1].as<int>());
  TEST_ASSERT_TRUE(def[1].as<int>() - def[0].as<int>() >= iv["minGap"].as<int>());
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

void test_shifty_interval_bounds_and_help() {
  auto iv = findById("shifty")["interval"];
  TEST_ASSERT_TRUE(iv.is<JsonObject>());
  TEST_ASSERT_EQUAL_INT(300,   iv["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(18000, iv["max"].as<int>());
  auto def = iv["default"].as<JsonArray>();
  TEST_ASSERT_EQUAL_INT(300, def[0].as<int>());
  TEST_ASSERT_EQUAL_INT(900, def[1].as<int>());
  TEST_ASSERT_EQUAL_STRING("A random time in this range is picked before each trigger.",
                           iv["help"].as<const char*>());
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
  TEST_ASSERT_FALSE(hasSections);
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
      TEST_ASSERT_EQUAL_STRING("Dreamy", p["leftLabel"].as<const char*>());
      TEST_ASSERT_EQUAL_STRING("Gentle", p["rightLabel"].as<const char*>());
      TEST_ASSERT_EQUAL_STRING(
          "Gentle: calm fades over a few seconds. Dreamy: slow drifting fades up to tens of seconds.",
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

void test_pulse_size_is_small_large_scale() {
  JsonObject e = findById("pulse");
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "size") {
      TEST_ASSERT_TRUE(p["unit"].isNull());
      TEST_ASSERT_FALSE(p["invert"].as<bool>());
      TEST_ASSERT_EQUAL_INT(1, p["min"].as<int>());
      auto maxVal = p["max"];
      TEST_ASSERT_FALSE(maxVal.is<JsonObject>());
      TEST_ASSERT_EQUAL_INT(10, maxVal.as<int>());
      TEST_ASSERT_EQUAL_INT(4, p["default"].as<int>());
      TEST_ASSERT_EQUAL_STRING("small", p["leftLabel"].as<const char*>());
      TEST_ASSERT_EQUAL_STRING("large", p["rightLabel"].as<const char*>());
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
  TEST_ASSERT_EQUAL_size_t(18, opts.size());
  TEST_ASSERT_EQUAL_STRING("Linear", opts[0]["label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Random", opts[17]["label"].as<const char*>());
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
  TEST_ASSERT_EQUAL_size_t(18, easing["options"].as<JsonArray>().size());
}

void test_shifty_easing_param_defaults_linear() {
  JsonObject easing = findParam(findById("shifty"), "easing");
  TEST_ASSERT_FALSE(easing.isNull());
  TEST_ASSERT_EQUAL_STRING("enum", easing["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Motion", easing["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, easing["default"].as<int>());  // Linear reproduces today's linear fade
  TEST_ASSERT_EQUAL_size_t(18, easing["options"].as<JsonArray>().size());
}

void test_breathing_easing_param_defaults_smooth() {
  JsonObject easing = findParam(findById("breathing"), "easing");
  TEST_ASSERT_FALSE(easing.isNull());
  TEST_ASSERT_EQUAL_STRING("enum", easing["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Motion", easing["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(1, easing["default"].as<int>());  // Smooth approximates the sine breath curve
  TEST_ASSERT_EQUAL_size_t(18, easing["options"].as<JsonArray>().size());
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

void test_pulse_direction_param() {
  JsonObject e = findById("pulse");
  JsonObject travel;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "travel") { travel = p; break; }
  }
  TEST_ASSERT_FALSE(travel.isNull());
  TEST_ASSERT_EQUAL_STRING("enum", travel["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Direction", travel["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, travel["default"].as<int>());
  auto opts = travel["options"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(2, opts.size());
  TEST_ASSERT_EQUAL_STRING("Up", opts[0]["label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Down", opts[1]["label"].as<const char*>());
}

static bool hasOpacity(const ExpressionDescriptor& d) {
  for (const auto& p : d.params)
    if (std::strcmp(p.key, "opacity") == 0)
      return p.min == 10 && p.max.v == 100 && p.def.v == 100;
  return false;
}
void test_all_types_expose_opacity() {
  TEST_ASSERT_TRUE(hasOpacity(kBreathingDescriptorData));
  TEST_ASSERT_TRUE(hasOpacity(kSpottyDescriptorData));
  TEST_ASSERT_TRUE(hasOpacity(kPulseDescriptorData));
  TEST_ASSERT_TRUE(hasOpacity(kGlitchyDescriptorData));
  TEST_ASSERT_TRUE(hasOpacity(kShiftyDescriptorData));
}

void test_shimmer_is_continuous_and_zoneable() {
  TEST_ASSERT_TRUE(findById("flicker")["continuous"].as<bool>());
  auto zone = findById("flicker")["zone"];
  TEST_ASSERT_TRUE(zone.is<JsonObject>());
  TEST_ASSERT_TRUE(zone["optional"].as<bool>());
}

void test_shimmer_is_not_advanced() {
  TEST_ASSERT_TRUE(findById("flicker")["advanced"].isNull());
  TEST_ASSERT_TRUE(findById("spotty")["advanced"].isNull());
  TEST_ASSERT_TRUE(findById("breathing")["advanced"].isNull());
}

void test_shimmer_fire_enum() {
  JsonObject fire = findParam(findById("flicker"), "fire");
  TEST_ASSERT_FALSE(fire.isNull());
  TEST_ASSERT_EQUAL_STRING("enum", fire["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Style", fire["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(1, fire["default"].as<int>());  // Coals
  auto opts = fire["options"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(4, opts.size());
  TEST_ASSERT_EQUAL_STRING("Twinkle",   opts[0]["label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Coals",     opts[1]["label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Candle",    opts[2]["label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Campfire",  opts[3]["label"].as<const char*>());
}

void test_shimmer_has_no_colors_and_has_opacity() {
  TEST_ASSERT_EQUAL_INT(0, findById("flicker")["colors"]["max"].as<int>());
  TEST_ASSERT_TRUE(hasOpacity(kShimmerDescriptorData));
}

static void assertKeyStored(const char* key) {
  if (!key || !std::strcmp(key, "intervalMin") || !std::strcmp(key, "intervalMax")) return;
  TEST_ASSERT_TRUE_MESSAGE(expressionParamKeyIndex(key) >= 0, key);
}

// Stored configs carry a parameter as its index in kExpressionParamKeys, and
// a key missing from it is dropped at parse. Every key a descriptor offers
// the app has to be in the table.
void test_every_descriptor_key_has_a_stored_index() {
  const ExpressionDescriptor* all[] = {
      &kGlitchyDescriptorData, &kPulseDescriptorData, &kBreathingDescriptorData,
      &kShiftyDescriptorData,  &kSpottyDescriptorData, &kShimmerDescriptorData,
      &kBloomDescriptorData,   &kYawnDescriptorData};
  for (const ExpressionDescriptor* d : all) {
    for (const ParamSpec& p : d->params) assertKeyStored(p.key);
    for (const auto* r : {&d->interval, &d->duration}) {
      if (!r->has_value()) continue;
      assertKeyStored((*r)->minKey);
      assertKeyStored((*r)->maxKey);
    }
  }
  TEST_ASSERT_TRUE(expressionParamKeyIndex("posMin") >= 0);
  TEST_ASSERT_TRUE(expressionParamKeyIndex("posMax") >= 0);
  TEST_ASSERT_TRUE(expressionParamKeyIndex(kParamCascadeEnabled) >= 0);
  TEST_ASSERT_TRUE(expressionParamKeyIndex(kParamCascadeStaggerMs) >= 0);
  TEST_ASSERT_TRUE(expressionParamKeyIndex(kLoopParamKey) >= 0);
}

// The stored and wire parameter cap holds every key a type can carry: its
// params, its duration range keys, the three zone keys and the two cascade
// keys.
void test_every_type_fits_the_parameter_cap() {
  const ExpressionDescriptor* all[] = {
      &kGlitchyDescriptorData, &kPulseDescriptorData, &kBreathingDescriptorData,
      &kShiftyDescriptorData,  &kSpottyDescriptorData, &kShimmerDescriptorData,
      &kBloomDescriptorData,   &kYawnDescriptorData};
  for (const ExpressionDescriptor* d : all) {
    size_t keys = d->params.size() + 2;
    if (d->duration.has_value() && d->duration->minKey) keys += 2;
    if (d->hasZone) keys += 3;
    TEST_ASSERT_TRUE_MESSAGE(keys <= kMaxExpressionParams, d->id);
  }
}

// A Moment frame rides MSG_EVENT as `"type":"moment"`; an older lamp builds
// no transient for it only because no variant registers that id. Walks the
// same per-variant sets the runtime registration expands (standard, lioness
// and loaf register the base set).
void test_no_variant_registers_the_moment_sentinel() {
#define X(N) &k##N##DescriptorData,
  const std::vector<std::vector<const ExpressionDescriptor*>> variants = {
      {LAMPOS_BASE_EXPRESSIONS(X)},
      {LAMPOS_SNAFU_EXPRESSIONS(X)},
      {LAMPOS_STAFF_EXPRESSIONS(X)},
  };
#undef X
  for (const auto& registry : variants) {
    for (const ExpressionDescriptor* d : registry) {
      TEST_ASSERT_NOT_EQUAL(0, std::strcmp(d->id, "moment"));
    }
  }
}

// The table is append-only: an index already written to a lamp's NVS must
// keep naming the same key.
void test_param_key_table_prefix_is_pinned() {
  const char* pinned[] = {
      "breathSpeed",  "cascadeEnabled",   "cascadeStaggerMs", "count",
      "durationMax",  "durationMin",      "easing",           "fadeDuration",
      "fillMode",     "fire",             "fullStrip",        "loop",
      "opacity",      "posMax",           "posMin",           "pulseSpeed",
      "scatter",      "shiftDurationMax", "shiftDurationMin", "size",
      "spotSpeed"};
  TEST_ASSERT_TRUE(kExpressionParamKeyCount >= sizeof(pinned) / sizeof(pinned[0]));
  for (size_t i = 0; i < sizeof(pinned) / sizeof(pinned[0]); ++i) {
    TEST_ASSERT_EQUAL_STRING(pinned[i], kExpressionParamKeys[i]);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_descriptor_key_has_a_stored_index);
  RUN_TEST(test_param_key_table_prefix_is_pinned);
  RUN_TEST(test_no_variant_registers_the_moment_sentinel);
  RUN_TEST(test_every_type_fits_the_parameter_cap);
  RUN_TEST(test_all_five_ids_present);
  RUN_TEST(test_continuous_flags);
  RUN_TEST(test_glitchy_zone_optional);
  RUN_TEST(test_glitchy_scatter_always_active_literal_max);
  RUN_TEST(test_glitchy_has_no_size_param);
  RUN_TEST(test_glitchy_duration_range);
  RUN_TEST(test_glitchy_interval_bounds_and_min_gap);
  RUN_TEST(test_interval_help_present);
  RUN_TEST(test_shifty_fillmode_enum_no_zoning);
  RUN_TEST(test_shifty_fade_duration_range);
  RUN_TEST(test_shifty_zone_optional);
  RUN_TEST(test_shifty_duration_hold_time);
  RUN_TEST(test_shifty_interval_bounds_and_help);
  RUN_TEST(test_breathing_has_no_interval);
  RUN_TEST(test_breathing_params_present);
  RUN_TEST(test_breathing_speed_floor);
  RUN_TEST(test_spotty_speed_param);
  RUN_TEST(test_spotty_has_no_interval);
  RUN_TEST(test_spotty_count_and_size_caps);
  RUN_TEST(test_pulse_zone_optional);
  RUN_TEST(test_pulse_size_is_small_large_scale);
  RUN_TEST(test_spotty_zone_optional);
  RUN_TEST(test_breathing_zone_optional);
  RUN_TEST(test_pulse_speed_invert_and_labels);
  RUN_TEST(test_pulse_easing_param);
  RUN_TEST(test_spotty_easing_param_defaults_linear);
  RUN_TEST(test_shifty_easing_param_defaults_linear);
  RUN_TEST(test_breathing_easing_param_defaults_smooth);
  RUN_TEST(test_pulse_loop_param);
  RUN_TEST(test_pulse_direction_param);
  RUN_TEST(test_all_types_expose_opacity);
  RUN_TEST(test_shimmer_is_continuous_and_zoneable);
  RUN_TEST(test_shimmer_is_not_advanced);
  RUN_TEST(test_shimmer_fire_enum);
  RUN_TEST(test_shimmer_has_no_colors_and_has_opacity);
  return UNITY_END();
}
