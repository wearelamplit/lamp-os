// Local constexpr copies mirror the production descriptors (.make needs Arduino).
#include <unity.h>
#include <ArduinoJson.h>

#include "expressions/expression_registry.hpp"
#include "expressions/expression_schema.hpp"

#include "../../src/expressions/expression_registry.cpp"

using namespace lamp;

// ---- Glitchy ---------------------------------------------------------------

static constexpr ParamSpec kGlitchyParams[] = {
  { .key = "count", .kind = ParamKind::Int, .label = "Points",
    .min = 1, .max = Bound::pixels(10), .step = 1, .def = 1,
    .requiresZoning = true },
  { .key = "size", .kind = ParamKind::Int, .label = "Size",
    .min = 1, .max = Bound::pixels(), .step = 1, .def = 1,
    .requiresZoning = true },
};
static constexpr ExpressionDescriptor kGlitchy{
  .id = "glitchy",
  .name = "Glitchy",
  .continuous = false,
  .colors = { .max = 8, .label = "Colors" },
  .interval = RangeSpec{ .min = 60, .max = 900, .step = 30, .unit = "s",
                         .defLo = 60, .defHi = 900 },
  .duration = RangeSpec{ .min = 30, .max = 2000, .step = 30, .unit = "ms",
                         .defLo = 30, .defHi = 120, .label = "Glitch duration" },
  .hasZone = true,
  .zoneOptional = true,
  .params = kGlitchyParams,
};

// ---- Pulse -----------------------------------------------------------------

static constexpr ParamSpec kPulseParams[] = {
  { .key = "pulseSpeed", .kind = ParamKind::Int, .label = "Pulse speed",
    .min = 1, .max = 10, .step = 1, .def = 3, .unit = "s",
    .invert = true, .leftLabel = "slow", .rightLabel = "fast" },
  { .key = "size", .kind = ParamKind::Int, .label = "Size",
    .min = 1, .max = Bound::pixels(), .step = 1, .def = 15 },
};
static constexpr ExpressionDescriptor kPulse{
  .id = "pulse",
  .name = "Pulse",
  .continuous = false,
  .colors = { .max = 8, .label = "Colors" },
  .interval = RangeSpec{ .min = 60, .max = 900, .step = 30, .unit = "s",
                         .defLo = 60, .defHi = 900 },
  .hasZone = true,
  .zoneOptional = false,
  .params = kPulseParams,
};

// ---- Breathing -------------------------------------------------------------

static constexpr ParamSpec kBreathingParams[] = {
  { .key = "breathSpeed", .kind = ParamKind::Int, .label = "Breath cycle length",
    .min = 1, .max = 60, .step = 1, .def = 10, .unit = "s",
    .invert = true, .leftLabel = "slow", .rightLabel = "fast" },
  { .key = "count", .kind = ParamKind::Int, .label = "Points",
    .min = 1, .max = Bound::pixels(10), .step = 1, .def = 1 },
  { .key = "size", .kind = ParamKind::Int, .label = "Size",
    .min = 1, .max = Bound::pixels(), .step = 1, .def = 32767 },
  { .key = "scatter", .kind = ParamKind::Int, .label = "Scatter",
    .min = 0, .max = 100, .step = 1, .def = 0, .unit = "%",
    .leftLabel = "Sync", .rightLabel = "Embers" },
};
static constexpr ExpressionDescriptor kBreathing{
  .id = "breathing",
  .name = "Breathing",
  .continuous = true,
  .pausesWispOverride = true,
  .colors = { .max = 8, .label = "Colors" },
  .hasZone = true,
  .zoneOptional = false,
  .params = kBreathingParams,
};

// ---- Shifty ----------------------------------------------------------------

static constexpr EnumOption kShiftyFillOpts[] = {
  { .value = 0, .label = "Uniform", .zoning = false },
  { .value = 1, .label = "Up",      .zoning = true  },
  { .value = 2, .label = "Down",    .zoning = true  },
  { .value = 3, .label = "Bloom",   .zoning = true  },
};
static constexpr ParamSpec kShiftyParams[] = {
  { .key = "fillMode", .kind = ParamKind::Enum, .label = "Fill",
    .min = 0, .max = 3, .step = 1, .def = 0,
    .options = kShiftyFillOpts },
  { .key = "fadeDuration", .kind = ParamKind::Int, .label = "Fade duration",
    .min = 10, .max = 300, .step = 1, .def = 60, .unit = "s",
    .leftLabel = "quick", .rightLabel = "slow" },
};
static constexpr ExpressionDescriptor kShifty{
  .id = "shifty",
  .name = "Shifty",
  .continuous = true,
  .pausesWispOverride = true,
  .colors = { .max = 8, .label = "Colors" },
  .interval = RangeSpec{ .min = 60, .max = 900, .step = 30, .unit = "s",
                         .defLo = 60, .defHi = 900 },
  .duration = RangeSpec{ .min = 60, .max = 1800, .step = 30, .unit = "s",
                         .defLo = 300, .defHi = 600, .label = "Hold time" },
  .hasZone = true,
  .zoneOptional = false,
  .params = kShiftyParams,
};

// ---- Spotty ----------------------------------------------------------------

static constexpr ParamSpec kSpottyParams[] = {
  { .key = "count", .kind = ParamKind::Int, .label = "Points",
    .min = 1, .max = Bound::pixels(10), .step = 1, .def = 3 },
  { .key = "size", .kind = ParamKind::Int, .label = "Size",
    .min = 1, .max = Bound::pixels(), .step = 1, .def = 4 },
  { .key = "spotSpeed", .kind = ParamKind::Int, .label = "Speed",
    .min = 1, .max = 10, .step = 1, .def = 3, .unit = "s",
    .invert = true, .leftLabel = "slow", .rightLabel = "fast" },
};
static constexpr ExpressionDescriptor kSpotty{
  .id = "spotty",
  .name = "Spotty",
  .continuous = false,
  .pausesWispOverride = true,
  .colors = { .max = 8, .label = "Colors" },
  .interval = RangeSpec{ .min = 60, .max = 900, .step = 30, .unit = "s",
                         .defLo = 60, .defHi = 900 },
  .hasZone = true,
  .zoneOptional = false,
  .params = kSpottyParams,
};

// ---- Test helpers ----------------------------------------------------------

static ExpressionRegistry g_reg;
static JsonDocument g_doc;

void setUp() {
  g_reg = ExpressionRegistry{};
  g_reg.add(kGlitchy);
  g_reg.add(kPulse);
  g_reg.add(kBreathing);
  g_reg.add(kShifty);
  g_reg.add(kSpotty);
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
  TEST_ASSERT_FALSE(findById("glitchy")["continuous"].as<bool>());
  TEST_ASSERT_FALSE(findById("pulse")["continuous"].as<bool>());
  TEST_ASSERT_FALSE(findById("spotty")["continuous"].as<bool>());
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

void test_glitchy_count_requires_zoning_pixel_capped_max() {
  JsonObject e = findById("glitchy");
  JsonObject countParam;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "count") { countParam = p; break; }
  }
  TEST_ASSERT_TRUE(countParam["requiresZoning"].as<bool>());
  auto maxVal = countParam["max"];
  TEST_ASSERT_TRUE(maxVal.is<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("pixels", maxVal["rel"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(10, maxVal["cap"].as<int>());
}

void test_glitchy_size_requires_zoning_pixel_relative_max() {
  JsonObject e = findById("glitchy");
  JsonObject sizeParam;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "size") { sizeParam = p; break; }
  }
  TEST_ASSERT_TRUE(sizeParam["requiresZoning"].as<bool>());
  auto maxVal = sizeParam["max"];
  TEST_ASSERT_TRUE(maxVal.is<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("pixels", maxVal["rel"].as<const char*>());
}

void test_glitchy_duration_range() {
  auto dur = findById("glitchy")["duration"];
  TEST_ASSERT_TRUE(dur.is<JsonObject>());
  TEST_ASSERT_EQUAL_INT(30,   dur["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(2000, dur["max"].as<int>());
  auto def = dur["default"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(2, def.size());
  TEST_ASSERT_EQUAL_INT(30,  def[0].as<int>());
  TEST_ASSERT_EQUAL_INT(120, def[1].as<int>());
}

void test_shifty_fillmode_enum_with_zoning() {
  JsonObject e = findById("shifty");
  JsonObject fillParam;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "fillMode") { fillParam = p; break; }
  }
  TEST_ASSERT_EQUAL_STRING("enum", fillParam["type"].as<const char*>());
  auto opts = fillParam["options"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(4, opts.size());
  TEST_ASSERT_FALSE(opts[0]["zoning"].as<bool>());  // Uniform: no zone
  TEST_ASSERT_TRUE(opts[1]["zoning"].as<bool>());   // Up: zoning
  TEST_ASSERT_TRUE(opts[2]["zoning"].as<bool>());   // Down: zoning
  TEST_ASSERT_TRUE(opts[3]["zoning"].as<bool>());   // Bloom: zoning
}

void test_shifty_duration_hold_time() {
  auto dur = findById("shifty")["duration"];
  TEST_ASSERT_TRUE(dur.is<JsonObject>());
  TEST_ASSERT_EQUAL_INT(60,   dur["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(1800, dur["max"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Hold time", dur["label"].as<const char*>());
  auto def = dur["default"].as<JsonArray>();
  TEST_ASSERT_EQUAL_INT(300, def[0].as<int>());
  TEST_ASSERT_EQUAL_INT(600, def[1].as<int>());
}

void test_breathing_has_no_interval() {
  TEST_ASSERT_TRUE(findById("breathing")["interval"].isNull());
}

void test_breathing_params_present() {
  JsonObject e = findById("breathing");
  bool hasBreathSpeed = false, hasCount = false, hasSize = false, hasScatter = false;
  for (JsonObject p : e["params"].as<JsonArray>()) {
    const char* k = p["key"].as<const char*>();
    if (std::string(k) == "breathSpeed") hasBreathSpeed = true;
    if (std::string(k) == "count")       hasCount = true;
    if (std::string(k) == "size")        hasSize = true;
    if (std::string(k) == "scatter")     hasScatter = true;
  }
  TEST_ASSERT_TRUE(hasBreathSpeed);
  TEST_ASSERT_TRUE(hasCount);
  TEST_ASSERT_TRUE(hasSize);
  TEST_ASSERT_TRUE(hasScatter);
}

void test_breathing_scatter_labels() {
  JsonObject e = findById("breathing");
  for (JsonObject p : e["params"].as<JsonArray>()) {
    if (std::string(p["key"].as<const char*>()) == "scatter") {
      TEST_ASSERT_EQUAL_STRING("Sync",   p["leftLabel"].as<const char*>());
      TEST_ASSERT_EQUAL_STRING("Embers", p["rightLabel"].as<const char*>());
      return;
    }
  }
  TEST_FAIL_MESSAGE("scatter param not found");
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
      break;
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_spotty_has_interval() {
  auto interval = findById("spotty")["interval"];
  TEST_ASSERT_TRUE(interval.is<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("s", interval["unit"].as<const char*>());
}

void test_pulse_has_zone_not_optional() {
  JsonObject e = findById("pulse");
  auto zone = e["zone"];
  TEST_ASSERT_TRUE(zone.is<JsonObject>());
  TEST_ASSERT_TRUE(zone["optional"].isNull());
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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_all_five_ids_present);
  RUN_TEST(test_continuous_flags);
  RUN_TEST(test_pauses_wisp_override_flags);
  RUN_TEST(test_glitchy_zone_optional);
  RUN_TEST(test_glitchy_count_requires_zoning_pixel_capped_max);
  RUN_TEST(test_glitchy_size_requires_zoning_pixel_relative_max);
  RUN_TEST(test_glitchy_duration_range);
  RUN_TEST(test_shifty_fillmode_enum_with_zoning);
  RUN_TEST(test_shifty_duration_hold_time);
  RUN_TEST(test_breathing_has_no_interval);
  RUN_TEST(test_breathing_params_present);
  RUN_TEST(test_breathing_scatter_labels);
  RUN_TEST(test_spotty_speed_param);
  RUN_TEST(test_spotty_has_interval);
  RUN_TEST(test_pulse_has_zone_not_optional);
  RUN_TEST(test_pulse_speed_invert_and_labels);
  return UNITY_END();
}
