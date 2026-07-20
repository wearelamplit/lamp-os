#include <unity.h>

#include <ArduinoJson.h>

#include <map>
#include <string>

#include "expressions/expression_registry.hpp"
#include "expressions/param_utils.hpp"

// Native tests don't build src/ — pull in the implementation directly.
#include "../../src/expressions/expression_registry.cpp"

using namespace lamp;

// -- Descriptor 1: interval, hasZone, zoneOptional, int param with
//    Bound::pixels(10) + requiresZoning, excludeTargets=[Base].
static constexpr ParamSpec kParams1[] = {
  { .key = "count", .kind = ParamKind::Int, .label = "Count",
    .min = 1, .max = Bound::pixels(10), .step = 1, .def = 4,
    .requiresZoning = true },
};

static constexpr Surface kExclude1[] = { Surface::Base };

static constexpr ExpressionDescriptor kDesc1{
  .id = "sparkle",
  .name = "Sparkle",
  .continuous = false,
  .colors = { .max = 2, .label = "Colors" },
  .interval = RangeSpec{ .min = 0, .max = 30, .step = 1, .unit = "s",
                         .defLo = 5, .defHi = 15, .label = "Interval",
                         .minKey = "intervalMin", .maxKey = "intervalMax" },
  .hasZone = true,
  .zoneOptional = true,
  .excludeTargets = kExclude1,
  .params = kParams1,
};

// -- Descriptor 2: continuous, enum param with zoning option,
//    inheritsSurface colors, excludeTargets=[Base].
static constexpr EnumOption kOpts2[] = {
  { .value = 0, .label = "Uniform", .zoning = false },
  { .value = 1, .label = "Zoned",   .zoning = true  },
};

static constexpr ParamSpec kParams2[] = {
  { .key = "style", .kind = ParamKind::Enum, .label = "Style",
    .min = 0, .max = 1, .step = 1, .def = 0,
    .options = kOpts2 },
};

static constexpr Surface kExclude2[] = { Surface::Base };

static constexpr ExpressionDescriptor kDesc2{
  .id = "wash",
  .name = "Wash",
  .continuous = true,
  .pausesWispOverride = true,
  .colors = { .max = 4, .label = "Wash Colors", .inheritsSurface = true },
  .excludeTargets = kExclude2,
  .params = kParams2,
};

// -- Descriptor 3: Bound::pixels() max, duration range, hasZone=true zoneOptional=false.
static constexpr ParamSpec kParams3[] = {
  { .key = "speed", .kind = ParamKind::Int, .label = "Speed",
    .min = 0, .max = Bound::pixels(), .step = 1, .def = 5 },
};

static constexpr ExpressionDescriptor kDesc3{
  .id = "ripple",
  .name = "Ripple",
  .continuous = true,
  .colors = { .max = 1 },
  .duration = RangeSpec{ .min = 1, .max = 60, .step = 1, .unit = "s",
                         .defLo = 5, .defHi = 30, .label = "Duration" },
  .hasZone = true,
  .zoneOptional = false,
  .params = kParams3,
};

// -- Descriptor 4: pixel-relative param defaults + duration range with keys.
static constexpr ParamSpec kParams4[] = {
  { .key = "size", .kind = ParamKind::Int, .label = "Size",
    .min = 0, .max = Bound::pixels(), .step = 1, .def = Bound::pixels() },
  { .key = "cap", .kind = ParamKind::Int, .label = "Cap",
    .min = 0, .max = Bound::pixels(), .step = 1, .def = Bound::pixels(10) },
};

static constexpr ExpressionDescriptor kDesc4{
  .id = "glow",
  .name = "Glow",
  .continuous = true,
  .colors = { .max = 1 },
  .duration = RangeSpec{ .min = 1, .max = 60, .step = 1, .unit = "s",
                         .defLo = 3, .defHi = 20, .label = "Duration",
                         .minKey = "durationMin", .maxKey = "durationMax" },
  .params = kParams4,
};

void setUp(void) {}
void tearDown(void) {}

void test_add_find_remove() {
  ExpressionRegistry reg;
  reg.add(kDesc1);
  reg.add(kDesc2);

  TEST_ASSERT_EQUAL_size_t(2, reg.all().size());
  TEST_ASSERT_NOT_NULL(reg.find("sparkle"));
  TEST_ASSERT_NOT_NULL(reg.find("wash"));
  TEST_ASSERT_NULL(reg.find("nope"));

  reg.remove("sparkle");
  TEST_ASSERT_EQUAL_size_t(1, reg.all().size());
  TEST_ASSERT_NULL(reg.find("sparkle"));

  // remove non-existent is a no-op
  reg.remove("ghost");
  TEST_ASSERT_EQUAL_size_t(1, reg.all().size());
}

void test_add_replaces_existing_id() {
  ExpressionRegistry reg;
  reg.add(kDesc1);
  reg.add(kDesc1);  // same id, last wins
  TEST_ASSERT_EQUAL_size_t(1, reg.all().size());
}

void test_apply_defaults_fills_missing_and_preserves_present() {
  ExpressionRegistry reg;
  std::map<std::string, uint32_t> params;
  params["count"] = 99;  // already present — must not be overwritten

  reg.applyDefaults(kDesc1, params, 30);
  TEST_ASSERT_EQUAL_UINT32(99, params["count"]);  // untouched
  // interval range folds into its minKey/maxKey
  TEST_ASSERT_EQUAL_UINT32(5,  params["intervalMin"]);
  TEST_ASSERT_EQUAL_UINT32(15, params["intervalMax"]);

  // kDesc2 has "style" with def=0
  reg.applyDefaults(kDesc2, params, 30);
  TEST_ASSERT_TRUE(params.count("style") > 0);
  TEST_ASSERT_EQUAL_UINT32(0, params["style"]);
}

void test_apply_defaults_resolves_bounds_and_folds_ranges() {
  ExpressionRegistry reg;

  std::map<std::string, uint32_t> params;
  reg.applyDefaults(kDesc4, params, 24);
  TEST_ASSERT_EQUAL_UINT32(24, params["size"]);         // pixels → window
  TEST_ASSERT_EQUAL_UINT32(10, params["cap"]);          // pixels(10), window > cap
  TEST_ASSERT_EQUAL_UINT32(3,  params["durationMin"]);  // duration range fold
  TEST_ASSERT_EQUAL_UINT32(20, params["durationMax"]);

  std::map<std::string, uint32_t> small;
  reg.applyDefaults(kDesc4, small, 6);
  TEST_ASSERT_EQUAL_UINT32(6, small["size"]);           // pixels → window
  TEST_ASSERT_EQUAL_UINT32(6, small["cap"]);            // min(window, 10)

  std::map<std::string, uint32_t> preset;
  preset["size"] = 99;
  preset["durationMin"] = 42;
  reg.applyDefaults(kDesc4, preset, 24);
  TEST_ASSERT_EQUAL_UINT32(99, preset["size"]);         // present key never overwritten
  TEST_ASSERT_EQUAL_UINT32(42, preset["durationMin"]);
}

void test_serialize_catalog_schema_version_and_count() {
  ExpressionRegistry reg;
  reg.add(kDesc1);
  reg.add(kDesc2);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  TEST_ASSERT_EQUAL_INT(1, doc["schemaVersion"].as<int>());
  TEST_ASSERT_EQUAL_size_t(2, doc["expressions"].as<JsonArray>().size());
}

void test_serialize_pixelscapped_bound_is_object() {
  ExpressionRegistry reg;
  reg.add(kDesc1);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto maxVal = doc["expressions"][0]["params"][0]["max"];
  TEST_ASSERT_TRUE(maxVal.is<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("pixels", maxVal["rel"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(10, maxVal["cap"].as<int>());
}

void test_serialize_literal_bound_is_number() {
  ExpressionRegistry reg;
  reg.add(kDesc2);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto maxVal = doc["expressions"][0]["params"][0]["max"];
  TEST_ASSERT_TRUE(maxVal.is<int>());
  TEST_ASSERT_EQUAL_INT(1, maxVal.as<int>());
}

void test_serialize_param_default_literal_is_number() {
  ExpressionRegistry reg;
  reg.add(kDesc1);  // count def = 4 (literal)

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto def = doc["expressions"][0]["params"][0]["default"];
  TEST_ASSERT_TRUE(def.is<int>());
  TEST_ASSERT_EQUAL_INT(4, def.as<int>());
}

void test_serialize_param_default_pixels_is_object() {
  ExpressionRegistry reg;
  reg.add(kDesc4);  // size def = Bound::pixels()

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto def = doc["expressions"][0]["params"][0]["default"];
  TEST_ASSERT_TRUE(def.is<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("pixels", def["rel"].as<const char*>());
  TEST_ASSERT_TRUE(def["cap"].isNull());
}

void test_serialize_param_default_pixelscapped_is_object() {
  ExpressionRegistry reg;
  reg.add(kDesc4);  // cap def = Bound::pixels(10)

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto def = doc["expressions"][0]["params"][1]["default"];
  TEST_ASSERT_TRUE(def.is<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("pixels", def["rel"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(10, def["cap"].as<int>());
}

void test_serialize_range_min_max_keys() {
  ExpressionRegistry reg;
  reg.add(kDesc1);  // interval carries minKey/maxKey

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto interval = doc["expressions"][0]["interval"];
  TEST_ASSERT_EQUAL_STRING("intervalMin", interval["minKey"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("intervalMax", interval["maxKey"].as<const char*>());
}

void test_serialize_range_min_gap_present() {
  static constexpr ExpressionDescriptor kGapDesc{
    .id = "gappy", .name = "Gappy",
    .colors = { .max = 1 },
    .interval = RangeSpec{ .min = 600, .max = 18000, .step = 30, .unit = "s",
                           .defLo = 1800, .defHi = 7200, .minGap = 1800 },
  };
  ExpressionRegistry reg;
  reg.add(kGapDesc);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  TEST_ASSERT_EQUAL_INT(1800, doc["expressions"][0]["interval"]["minGap"].as<int>());
}

void test_serialize_range_min_gap_absent_when_zero() {
  ExpressionRegistry reg;
  reg.add(kDesc1);  // interval has no minGap

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  TEST_ASSERT_TRUE(doc["expressions"][0]["interval"]["minGap"].isNull());
}

void test_clamp_range_hi_gap_widens_narrow_pair() {
  TEST_ASSERT_EQUAL_UINT32(2400, clampRangeHiGap(600, 900, 1800, 18000));
  TEST_ASSERT_EQUAL_UINT32(2400, clampRangeHiGap(600, 2400, 1800, 18000));
  TEST_ASSERT_EQUAL_UINT32(9000, clampRangeHiGap(600, 9000, 1800, 18000));
  // lo + minGap exceeds max -> clamped to max.
  TEST_ASSERT_EQUAL_UINT32(18000, clampRangeHiGap(17000, 17100, 1800, 18000));
}

void test_serialize_range_keys_absent_when_unset() {
  ExpressionRegistry reg;
  reg.add(kDesc3);  // duration has no keys

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto dur = doc["expressions"][0]["duration"];
  TEST_ASSERT_TRUE(dur["minKey"].isNull());
  TEST_ASSERT_TRUE(dur["maxKey"].isNull());
}

void test_serialize_param_type_strings() {
  ExpressionRegistry reg;
  reg.add(kDesc1);
  reg.add(kDesc2);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  TEST_ASSERT_EQUAL_STRING("int",  doc["expressions"][0]["params"][0]["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("enum", doc["expressions"][1]["params"][0]["type"].as<const char*>());
}

void test_serialize_enum_options_zoning() {
  ExpressionRegistry reg;
  reg.add(kDesc2);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto opts = doc["expressions"][0]["params"][0]["options"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(2, opts.size());
  TEST_ASSERT_FALSE(opts[0]["zoning"].as<bool>());
  TEST_ASSERT_TRUE(opts[1]["zoning"].as<bool>());
}

void test_serialize_zone_optional() {
  ExpressionRegistry reg;
  reg.add(kDesc1);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto zone = doc["expressions"][0]["zone"];
  TEST_ASSERT_TRUE(zone.is<JsonObject>());
  TEST_ASSERT_TRUE(zone["optional"].as<bool>());
}

void test_serialize_exclude_targets() {
  ExpressionRegistry reg;
  reg.add(kDesc1);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto excl = doc["expressions"][0]["excludeTargets"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(1, excl.size());
  TEST_ASSERT_EQUAL_STRING("base", excl[0].as<const char*>());
}

void test_serialize_pauses_wisp_override() {
  ExpressionRegistry reg;
  reg.add(kDesc1);  // pausesWispOverride unset
  reg.add(kDesc2);  // pausesWispOverride=true

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  TEST_ASSERT_TRUE(doc["expressions"][0]["pausesWispOverride"].isNull());
  TEST_ASSERT_TRUE(doc["expressions"][1]["pausesWispOverride"].as<bool>());
}

void test_serialize_colors_inherits_surface() {
  ExpressionRegistry reg;
  reg.add(kDesc2);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  TEST_ASSERT_TRUE(doc["expressions"][0]["colors"]["inheritsSurface"].as<bool>());
}

void test_serialize_interval_default_is_two_element_array() {
  ExpressionRegistry reg;
  reg.add(kDesc1);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto def = doc["expressions"][0]["interval"]["default"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(2, def.size());
  TEST_ASSERT_EQUAL_INT(5,  def[0].as<int>());
  TEST_ASSERT_EQUAL_INT(15, def[1].as<int>());
}

void test_serialize_pixels_bound_is_object_no_cap() {
  ExpressionRegistry reg;
  reg.add(kDesc3);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto maxVal = doc["expressions"][0]["params"][0]["max"];
  TEST_ASSERT_TRUE(maxVal.is<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("pixels", maxVal["rel"].as<const char*>());
  TEST_ASSERT_TRUE(maxVal["cap"].isNull());
}

void test_serialize_duration_range_spec() {
  ExpressionRegistry reg;
  reg.add(kDesc3);

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto dur = doc["expressions"][0]["duration"];
  TEST_ASSERT_TRUE(dur.is<JsonObject>());
  TEST_ASSERT_EQUAL_INT(1,  dur["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(60, dur["max"].as<int>());
  TEST_ASSERT_EQUAL_INT(1,  dur["step"].as<int>());
  TEST_ASSERT_EQUAL_STRING("s",        dur["unit"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Duration", dur["label"].as<const char*>());
  auto def = dur["default"].as<JsonArray>();
  TEST_ASSERT_EQUAL_size_t(2, def.size());
  TEST_ASSERT_EQUAL_INT(5,  def[0].as<int>());
  TEST_ASSERT_EQUAL_INT(30, def[1].as<int>());
}

void test_serialize_requires_zoning_key() {
  ExpressionRegistry reg;
  reg.add(kDesc1);  // count param has requiresZoning=true

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  TEST_ASSERT_TRUE(doc["expressions"][0]["params"][0]["requiresZoning"].as<bool>());
}

void test_serialize_zone_present_not_optional() {
  ExpressionRegistry reg;
  reg.add(kDesc3);  // hasZone=true, zoneOptional=false

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto zone = doc["expressions"][0]["zone"];
  TEST_ASSERT_TRUE(zone.is<JsonObject>());
  TEST_ASSERT_TRUE(zone["optional"].isNull());
}

void test_serialize_zone_absent_when_no_zone() {
  ExpressionRegistry reg;
  reg.add(kDesc2);  // hasZone=false

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  TEST_ASSERT_TRUE(doc["expressions"][0]["zone"].isNull());
}

void test_serialize_interval_unit_and_label() {
  ExpressionRegistry reg;
  reg.add(kDesc1);  // interval.unit="s", interval.label="Interval"

  JsonDocument doc;
  deserializeJson(doc, reg.serializeCatalog());

  auto interval = doc["expressions"][0]["interval"];
  TEST_ASSERT_EQUAL_STRING("s",        interval["unit"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Interval", interval["label"].as<const char*>());
}

void test_apply_defaults_negative_literal_clamps_to_zero() {
  static constexpr ParamSpec kNegParam[] = {
    { .key = "neg", .kind = ParamKind::Int, .label = "Neg",
      .def = Bound(-3) },
  };
  static constexpr ExpressionDescriptor kNegDesc{
    .id = "neg_test",
    .name = "NegTest",
    .colors = { .max = 1 },
    .params = kNegParam,
  };
  ExpressionRegistry reg;
  std::map<std::string, uint32_t> params;
  reg.applyDefaults(kNegDesc, params, 20);
  TEST_ASSERT_EQUAL_UINT32(0, params["neg"]);
}

void test_apply_defaults_does_not_overwrite_duration_max_key() {
  ExpressionRegistry reg;
  std::map<std::string, uint32_t> params;
  params["durationMax"] = 99;
  reg.applyDefaults(kDesc4, params, 24);
  TEST_ASSERT_EQUAL_UINT32(99, params["durationMax"]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_add_find_remove);
  RUN_TEST(test_add_replaces_existing_id);
  RUN_TEST(test_apply_defaults_fills_missing_and_preserves_present);
  RUN_TEST(test_apply_defaults_resolves_bounds_and_folds_ranges);
  RUN_TEST(test_serialize_catalog_schema_version_and_count);
  RUN_TEST(test_serialize_pixelscapped_bound_is_object);
  RUN_TEST(test_serialize_literal_bound_is_number);
  RUN_TEST(test_serialize_param_default_literal_is_number);
  RUN_TEST(test_serialize_param_default_pixels_is_object);
  RUN_TEST(test_serialize_param_default_pixelscapped_is_object);
  RUN_TEST(test_serialize_range_min_max_keys);
  RUN_TEST(test_serialize_range_min_gap_present);
  RUN_TEST(test_serialize_range_min_gap_absent_when_zero);
  RUN_TEST(test_clamp_range_hi_gap_widens_narrow_pair);
  RUN_TEST(test_serialize_range_keys_absent_when_unset);
  RUN_TEST(test_serialize_param_type_strings);
  RUN_TEST(test_serialize_enum_options_zoning);
  RUN_TEST(test_serialize_zone_optional);
  RUN_TEST(test_serialize_exclude_targets);
  RUN_TEST(test_serialize_pauses_wisp_override);
  RUN_TEST(test_serialize_colors_inherits_surface);
  RUN_TEST(test_serialize_interval_default_is_two_element_array);
  RUN_TEST(test_serialize_pixels_bound_is_object_no_cap);
  RUN_TEST(test_serialize_duration_range_spec);
  RUN_TEST(test_serialize_requires_zoning_key);
  RUN_TEST(test_serialize_zone_present_not_optional);
  RUN_TEST(test_serialize_zone_absent_when_no_zone);
  RUN_TEST(test_serialize_interval_unit_and_label);
  RUN_TEST(test_apply_defaults_negative_literal_clamps_to_zero);
  RUN_TEST(test_apply_defaults_does_not_overwrite_duration_max_key);
  return UNITY_END();
}
