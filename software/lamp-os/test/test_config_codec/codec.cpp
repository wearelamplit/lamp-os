// Native-host tests for config_codec: the JSON <-> model parse/serialize for
// the persisted cfg blob. Covers field defaults, the per-segment px/colors
// model, the Σ≤255 clamp, the home-mode migration, and a full serialize/parse
// round trip.

#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "config/config_codec.hpp"

// Native tests don't build src/, so compile the real implementation (and its
// color dependency) into this TU, same pattern as test_firmware_signature.
#include "../../src/util/color.cpp"
#include "../../src/config/config_codec.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

namespace {
struct Model {
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;
  HomeModeSettings homeMode;
};

bool parseInto(const char* json, Model& m) {
  JsonDocument doc;
  deserializeJson(doc, json);
  return config_codec::fromJson(doc.as<JsonObject>(), m.lamp, m.base, m.shade,
                                m.expressions, m.homeMode);
}

std::string expressionEntry(const char* type, int target, int colors, int params) {
  std::string e = std::string("{\"type\":\"") + type +
                  "\",\"enabled\":true,\"target\":" + std::to_string(target);
  for (int i = 0; i < params; ++i) {
    e += std::string(",\"") + kExpressionParamKeys[i % kExpressionParamKeyCount] +
         "\":" + std::to_string(i);
  }
  e += ",\"colors\":[";
  for (int i = 0; i < colors; ++i) e += std::string(i ? "," : "") + "\"#ff000000\"";
  return e + "]}";
}
}  // namespace

void test_empty_blob_uses_class_defaults() {
  Model m;
  parseInto("{}", m);
  TEST_ASSERT_EQUAL_STRING("stray", m.lamp.name.c_str());
  TEST_ASSERT_EQUAL_UINT8(100, m.lamp.brightness);
  TEST_ASSERT_EQUAL_UINT(1, m.base.segments.size());
  TEST_ASSERT_EQUAL_UINT(1, m.base.broadcastColors().size());
  TEST_ASSERT_TRUE(m.base.broadcastColors()[0] == kBaseDefaultColor);
  TEST_ASSERT_EQUAL_UINT(1, m.shade.segments.size());
  TEST_ASSERT_EQUAL_UINT(1, m.shade.broadcastColors().size());
  TEST_ASSERT_TRUE(m.shade.broadcastColors()[0] == kShadeDefaultColor);
}

void test_name_stored_lowercase() {
  Model mixed;
  parseInto("{\"lamp\":{\"name\":\"Jacko\"}}", mixed);
  TEST_ASSERT_EQUAL_STRING("jacko", mixed.lamp.name.c_str());

  // Whitespace is dropped, not preserved: the name composes the setup AP's
  // SSID, the BLE advert name and the mDNS host.
  Model words;
  parseInto("{\"lamp\":{\"name\":\"LIVING ROOM\"}}", words);
  TEST_ASSERT_EQUAL_STRING("livingroom", words.lamp.name.c_str());

  Model padded;
  parseInto("{\"lamp\":{\"name\":\"gramp \"}}", padded);
  TEST_ASSERT_EQUAL_STRING("gramp", padded.lamp.name.c_str());
}

void test_apbootminutes_defaults_and_round_trip() {
  Model absent;
  parseInto("{}", absent);
  TEST_ASSERT_EQUAL_UINT8(0, absent.lamp.apBootMinutes);

  for (uint8_t v : {(uint8_t)0, (uint8_t)5, (uint8_t)10}) {
    Model a;
    a.lamp.apBootMinutes = v;
    JsonDocument out;
    config_codec::toJson(out.to<JsonObject>(), a.lamp, a.base, a.shade,
                         a.expressions, a.homeMode);
    std::string serialized;
    serializeJson(out, serialized);
    Model b;
    parseInto(serialized.c_str(), b);
    TEST_ASSERT_EQUAL_UINT8(v, b.lamp.apBootMinutes);
  }
}

void test_brightnessceiling_defaults_and_round_trip() {
  Model absent;
  parseInto("{}", absent);
  TEST_ASSERT_EQUAL_UINT8(170, absent.lamp.brightnessCeiling);
  for (uint8_t v : {(uint8_t)120, (uint8_t)170, (uint8_t)230}) {
    Model a; a.lamp.brightnessCeiling = v;
    JsonDocument out;
    config_codec::toJson(out.to<JsonObject>(), a.lamp, a.base, a.shade, a.expressions, a.homeMode);
    std::string serialized; serializeJson(out, serialized);
    Model b; parseInto(serialized.c_str(), b);
    TEST_ASSERT_EQUAL_UINT8(v, b.lamp.brightnessCeiling);
  }
}

void test_socialmode_out_of_range_falls_back_to_ambivert() {
  Model m;
  parseInto("{\"lamp\":{\"socialMode\":9}}", m);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SocialMode::Ambivert),
                          static_cast<uint8_t>(m.lamp.socialMode));
}

void test_three_segment_shade_round_trip() {
  const char* json =
      "{\"shade\":{\"segments\":["
      "{\"name\":\"Small\",\"px\":16,\"colors\":[\"#11223300\"]},"
      "{\"name\":\"Medium\",\"px\":12,\"colors\":[\"#44556600\"]},"
      "{\"name\":\"Big\",\"px\":9,\"colors\":[\"#77889900\"]}]}}";
  Model a;
  parseInto(json, a);
  TEST_ASSERT_EQUAL_UINT(3, a.shade.segments.size());
  TEST_ASSERT_EQUAL_UINT8(37, a.shade.sumPx());

  JsonDocument out;
  config_codec::toJson(out.to<JsonObject>(), a.lamp, a.base, a.shade,
                       a.expressions, a.homeMode);
  std::string serialized;
  serializeJson(out, serialized);

  Model b;
  parseInto(serialized.c_str(), b);
  TEST_ASSERT_EQUAL_UINT(3, b.shade.segments.size());
  TEST_ASSERT_EQUAL_STRING("Medium", b.shade.segments[1].name.c_str());
  TEST_ASSERT_EQUAL_UINT8(12, b.shade.segments[1].px);
  TEST_ASSERT_TRUE(b.shade.segments[2].colors[0] == hexStringToColor("#77889900"));
  TEST_ASSERT_EQUAL_UINT8(37, b.shade.sumPx());
}

void test_sum_px_clamped_to_255() {
  // A blob whose segment px sum exceeds 255 must load clamped so boot
  // buffer-sizing (uint8 pixelCount) can't overflow.
  const char* json =
      "{\"base\":{\"segments\":["
      "{\"px\":200,\"colors\":[\"#11111100\"]},"
      "{\"px\":200,\"colors\":[\"#22222200\"]}]}}";
  Model m;
  parseInto(json, m);
  TEST_ASSERT_EQUAL_UINT8(255, m.base.sumPx());
  TEST_ASSERT_EQUAL_UINT8(200, m.base.segments[0].px);
  TEST_ASSERT_EQUAL_UINT8(55, m.base.segments[1].px);
}

void test_homemode_enabled_migration() {
  Model withSsid;
  parseInto("{\"homeMode\":{\"ssid\":\"net\"}}", withSsid);
  TEST_ASSERT_TRUE(withSsid.homeMode.enabled);  // absent enabled + ssid → on

  Model noSsid;
  parseInto("{\"homeMode\":{\"ssid\":\"\"}}", noSsid);
  TEST_ASSERT_FALSE(noSsid.homeMode.enabled);
}

void test_round_trip_preserves_fields() {
  const char* json =
      "{\"lamp\":{\"name\":\"floor\",\"brightness\":42,\"socialMode\":2},"
      "\"base\":{\"ac\":0,\"segments\":[{\"name\":\"Base\",\"px\":10,"
      "\"colors\":[\"#11223300\"]}]},"
      "\"shade\":{\"segments\":[{\"name\":\"Shade\",\"px\":5,"
      "\"colors\":[\"#aabbcc00\"]}]},"
      "\"homeMode\":{\"ssid\":\"net\",\"brightness\":30,\"enabled\":true},"
      "\"expressions\":[{\"type\":\"twinkle\",\"enabled\":true,"
      "\"intervalMin\":5,\"intervalMax\":9,\"target\":2,\"speed\":3,"
      "\"colors\":[\"#ff000000\"]}]}";
  Model a;
  parseInto(json, a);

  JsonDocument out;
  config_codec::toJson(out.to<JsonObject>(), a.lamp, a.base, a.shade,
                       a.expressions, a.homeMode);
  std::string serialized;
  serializeJson(out, serialized);

  Model b;
  parseInto(serialized.c_str(), b);

  TEST_ASSERT_EQUAL_STRING("floor", b.lamp.name.c_str());
  TEST_ASSERT_EQUAL_UINT8(42, b.lamp.brightness);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SocialMode::Extrovert),
                          static_cast<uint8_t>(b.lamp.socialMode));
  TEST_ASSERT_EQUAL_UINT8(10, b.base.sumPx());
  TEST_ASSERT_TRUE(b.base.broadcastColors()[0] == hexStringToColor("#11223300"));
  TEST_ASSERT_EQUAL_UINT8(5, b.shade.sumPx());
  TEST_ASSERT_TRUE(b.shade.broadcastColors()[0] == hexStringToColor("#aabbcc00"));
  TEST_ASSERT_EQUAL_STRING("net", b.homeMode.ssid.c_str());
  TEST_ASSERT_EQUAL_UINT8(30, b.homeMode.brightness);
  TEST_ASSERT_TRUE(b.homeMode.enabled);
  TEST_ASSERT_EQUAL_UINT(1, b.expressions.expressions.size());
  TEST_ASSERT_EQUAL_STRING("twinkle", b.expressions.expressions[0].type.c_str());
  TEST_ASSERT_TRUE(b.expressions.expressions[0].enabled);
  TEST_ASSERT_EQUAL_UINT(1, b.expressions.expressions[0].colors.size());
}

void test_expressions_within_caps_load_whole() {
  std::string json = "{\"lamp\":{},\"expressions\":[" +
                     expressionEntry("pulse", 1, 8, static_cast<int>(kMaxExpressionParams)) + "]}";
  Model m;
  TEST_ASSERT_TRUE(parseInto(json.c_str(), m));
  TEST_ASSERT_EQUAL_UINT(8, m.expressions.expressions[0].colors.size());
  TEST_ASSERT_EQUAL_UINT(kMaxExpressionParams, m.expressions.expressions[0].parameters.size());
}

// An over-cap stored config loads truncated (colours, parameters, entries)
// and reports it, so the lamp boots with a bounded config instead of growing
// it forever.
void test_over_cap_stored_expressions_truncate_on_load() {
  std::string json = "{\"lamp\":{},\"expressions\":[";
  for (size_t i = 0; i < kMaxExpressions + 5; ++i) {
    json += (i ? "," : "") +
            expressionEntry(("type" + std::to_string(i)).c_str(), 1, 60, 40);
  }
  json += "]}";
  Model m;
  TEST_ASSERT_FALSE(parseInto(json.c_str(), m));
  TEST_ASSERT_EQUAL_UINT(kMaxExpressions, m.expressions.expressions.size());
  TEST_ASSERT_EQUAL_UINT(kMaxExpressionColors,
                         m.expressions.expressions[0].colors.size());
  TEST_ASSERT_EQUAL_UINT(kMaxExpressionParams,
                         m.expressions.expressions[0].parameters.size());
}

// A repeated (type, target) loads once, the first kept.
void test_repeated_expression_loads_once() {
  std::string json = "{\"lamp\":{},\"expressions\":[" + expressionEntry("pulse", 1, 1, 0) +
                     "," + expressionEntry("pulse", 1, 2, 0) + "," +
                     expressionEntry("pulse", 2, 1, 0) + "]}";
  Model m;
  parseInto(json.c_str(), m);
  TEST_ASSERT_EQUAL_UINT(2, m.expressions.expressions.size());
  TEST_ASSERT_EQUAL_UINT(1, m.expressions.expressions[0].colors.size());
}

// A key no expression reads is dropped at parse, so it never costs RAM or NVS.
void test_unknown_param_keys_dropped() {
  JsonDocument doc;
  deserializeJson(doc, "{\"type\":\"pulse\",\"size\":3,\"bogusKey\":9}");
  ExpressionConfig cfg;
  TEST_ASSERT_TRUE(config_codec::parseExpression(doc.as<JsonObject>(), cfg, true));
  TEST_ASSERT_EQUAL_UINT(1, cfg.parameters.size());
  TEST_ASSERT_EQUAL_UINT32(3, cfg.getParameter("size", 0));
}

// A stored entry with an out-of-range target keeps its target: remove matches
// it exactly, so the user can still delete it.
void test_stored_out_of_range_target_kept() {
  std::string json = "{\"lamp\":{},\"expressions\":[" +
                     expressionEntry("pulse", 7, 1, 0) + "]}";
  Model m;
  TEST_ASSERT_TRUE(parseInto(json.c_str(), m));
  TEST_ASSERT_EQUAL_UINT8(7, m.expressions.expressions[0].target);
}

void test_parse_expression_upsert_defaults_enabled() {
  JsonDocument doc;
  deserializeJson(doc, "{\"type\":\"pulse\"}");
  ExpressionConfig cfg;
  TEST_ASSERT_TRUE(config_codec::parseExpression(doc.as<JsonObject>(), cfg, true));
  TEST_ASSERT_TRUE(cfg.enabled);
  TEST_ASSERT_EQUAL_UINT8(3, cfg.target);
  TEST_ASSERT_EQUAL_UINT32(60, cfg.intervalMin);
  TEST_ASSERT_TRUE(cfg.parameters.empty());
}

// Migration: old blobs (pre-7eca586f) had flat colors[]/px at the role level,
// no segments array. The codec must migrate them to a single segment so custom
// colors survive a firmware upgrade.

void test_old_format_base_colors_migrated() {
  // Old shape: {"base":{"colors":["#RRGGBBWW"],"px":35}, "shade":{...}}
  const char* json =
      "{\"lamp\":{\"name\":\"myLamp\"},"
      "\"base\":{\"colors\":[\"#11223344\",\"#55667788\"],\"px\":35},"
      "\"shade\":{\"colors\":[\"#aabbccdd\"],\"px\":38}}";
  Model m;
  parseInto(json, m);
  // Colors must survive — NOT fall to kBaseDefaultColor
  TEST_ASSERT_EQUAL_UINT(2, m.base.broadcastColors().size());
  TEST_ASSERT_TRUE(m.base.broadcastColors()[0] == hexStringToColor("#11223344"));
  TEST_ASSERT_TRUE(m.base.broadcastColors()[1] == hexStringToColor("#55667788"));
  TEST_ASSERT_EQUAL_UINT8(35, m.base.segments[0].px);
  // Migrated segment gets the role default name so the app never renders a
  // blank Base label.
  TEST_ASSERT_EQUAL_STRING("Base", m.base.segments[0].name.c_str());
}

void test_old_format_shade_colors_migrated() {
  const char* json =
      "{\"shade\":{\"colors\":[\"#aabbccdd\"],\"px\":38}}";
  Model m;
  parseInto(json, m);
  TEST_ASSERT_EQUAL_UINT(1, m.shade.broadcastColors().size());
  TEST_ASSERT_TRUE(m.shade.broadcastColors()[0] == hexStringToColor("#aabbccdd"));
  TEST_ASSERT_EQUAL_UINT8(38, m.shade.segments[0].px);
  TEST_ASSERT_EQUAL_STRING("Shade", m.shade.segments[0].name.c_str());
}

void test_old_format_no_colors_uses_class_default() {
  // A blob with neither segments nor colors should keep the class default.
  const char* json = "{\"base\":{\"ac\":0},\"shade\":{}}";
  Model m;
  parseInto(json, m);
  TEST_ASSERT_TRUE(m.base.broadcastColors()[0] == kBaseDefaultColor);
  TEST_ASSERT_TRUE(m.shade.broadcastColors()[0] == kShadeDefaultColor);
}

void test_new_format_segments_unchanged() {
  // Existing new-format blobs must parse identically to before.
  const char* json =
      "{\"base\":{\"segments\":[{\"name\":\"Base\",\"px\":35,"
      "\"colors\":[\"#11223344\"]}]},"
      "\"shade\":{\"segments\":[{\"name\":\"Shade\",\"px\":38,"
      "\"colors\":[\"#aabbccdd\"]}]}}";
  Model m;
  parseInto(json, m);
  TEST_ASSERT_EQUAL_UINT8(35, m.base.segments[0].px);
  TEST_ASSERT_TRUE(m.base.broadcastColors()[0] == hexStringToColor("#11223344"));
  TEST_ASSERT_EQUAL_UINT8(38, m.shade.segments[0].px);
  TEST_ASSERT_TRUE(m.shade.broadcastColors()[0] == hexStringToColor("#aabbccdd"));
  TEST_ASSERT_EQUAL_STRING("Base", m.base.segments[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Shade", m.shade.segments[0].name.c_str());
}

void test_named_segment_not_overwritten_by_role_default() {
  // An already-named segment (any name, not just the role default) must
  // survive parseSegments untouched.
  Model m;
  parseInto(
      "{\"base\":{\"segments\":[{\"name\":\"Left rail\",\"px\":20,"
      "\"colors\":[\"#11223344\"]}]}}",
      m);
  TEST_ASSERT_EQUAL_STRING("Left rail", m.base.segments[0].name.c_str());
}

// --- Home mode new fields ---

void test_networkbound_absent_with_ssid() {
  // Legacy blobs with an SSID but no networkBound: migrate to networkBound=true.
  Model m;
  parseInto("{\"homeMode\":{\"ssid\":\"net\"}}", m);
  TEST_ASSERT_TRUE(m.homeMode.networkBound);
}

void test_networkbound_absent_no_ssid() {
  Model m;
  parseInto("{\"homeMode\":{\"ssid\":\"\"}}", m);
  TEST_ASSERT_FALSE(m.homeMode.networkBound);
}

void test_networkbound_explicit_false_wins() {
  // Explicit false must win even if ssid is non-empty.
  Model m;
  parseInto("{\"homeMode\":{\"ssid\":\"net\",\"networkBound\":false}}", m);
  TEST_ASSERT_FALSE(m.homeMode.networkBound);
}

void test_disabled_expr_types_absent_defaults_to_glitchy() {
  Model m;
  parseInto("{\"homeMode\":{\"ssid\":\"net\"}}", m);
  TEST_ASSERT_EQUAL_UINT(1, m.homeMode.disabledExpressionTypes.size());
  TEST_ASSERT_EQUAL_STRING("glitchy", m.homeMode.disabledExpressionTypes[0].c_str());
}

void test_disabled_expr_types_explicit() {
  Model m;
  parseInto("{\"homeMode\":{\"disabledExpressionTypes\":[\"pulse\",\"shifty\"]}}", m);
  TEST_ASSERT_EQUAL_UINT(2, m.homeMode.disabledExpressionTypes.size());
  TEST_ASSERT_EQUAL_STRING("pulse", m.homeMode.disabledExpressionTypes[0].c_str());
  TEST_ASSERT_EQUAL_STRING("shifty", m.homeMode.disabledExpressionTypes[1].c_str());
}

void test_disabled_expr_types_empty_array_preserved() {
  Model m;
  parseInto("{\"homeMode\":{\"disabledExpressionTypes\":[]}}", m);
  TEST_ASSERT_EQUAL_UINT(0, m.homeMode.disabledExpressionTypes.size());
}

// A home-mode type that is not a type id (quotes, backslashes, too long) is
// dropped, so it never needs escaping in the stored config.
void test_disabled_expr_types_keep_only_ids() {
  Model m;
  parseInto("{\"homeMode\":{\"disabledExpressionTypes\":[\"gl\\\"itchy\",\"pulse\","
            "\"a\\\\b\",\"aaaaaaaaaaaaaaaa\"]}}",
            m);
  TEST_ASSERT_EQUAL_UINT(1, m.homeMode.disabledExpressionTypes.size());
  TEST_ASSERT_EQUAL_STRING("pulse", m.homeMode.disabledExpressionTypes[0].c_str());
}

void test_social_disabled_absent_defaults_true() {
  Model m;
  parseInto("{\"homeMode\":{\"ssid\":\"net\"}}", m);
  TEST_ASSERT_TRUE(m.homeMode.socialDisabled);
}

void test_social_disabled_explicit_false() {
  Model m;
  parseInto("{\"homeMode\":{\"socialDisabled\":false}}", m);
  TEST_ASSERT_FALSE(m.homeMode.socialDisabled);
}

void test_homemode_round_trip_new_fields() {
  const char* json =
      "{\"homeMode\":{\"ssid\":\"net\",\"brightness\":40,\"enabled\":true,"
      "\"networkBound\":true,\"socialDisabled\":false,"
      "\"disabledExpressionTypes\":[\"pulse\",\"breathing\"]}}";
  Model a;
  parseInto(json, a);

  JsonDocument out;
  config_codec::toJson(out.to<JsonObject>(), a.lamp, a.base, a.shade,
                       a.expressions, a.homeMode);
  std::string serialized;
  serializeJson(out, serialized);

  Model b;
  parseInto(serialized.c_str(), b);

  TEST_ASSERT_EQUAL_STRING("net", b.homeMode.ssid.c_str());
  TEST_ASSERT_TRUE(b.homeMode.networkBound);
  TEST_ASSERT_FALSE(b.homeMode.socialDisabled);
  TEST_ASSERT_EQUAL_UINT(2, b.homeMode.disabledExpressionTypes.size());
  TEST_ASSERT_EQUAL_STRING("pulse", b.homeMode.disabledExpressionTypes[0].c_str());
  TEST_ASSERT_EQUAL_STRING("breathing", b.homeMode.disabledExpressionTypes[1].c_str());
}

namespace {
// `rotate` parsed over `start`.
ColorRotation parsedOver(const char* rotateJson, ColorRotation start) {
  JsonDocument doc;
  deserializeJson(doc, rotateJson);
  config_codec::parseRotation(doc.as<JsonObjectConst>(), start);
  return start;
}

void assertRotation(bool on, uint8_t periodS, RotationDir dir, const ColorRotation& r) {
  TEST_ASSERT_EQUAL(on, r.on);
  TEST_ASSERT_EQUAL_UINT8(periodS, r.periodS);
  TEST_ASSERT_EQUAL(static_cast<int>(dir), static_cast<int>(r.dir));
}

template <class Role>
std::string sectionRotate(const Role& role) {
  JsonDocument doc;
  config_codec::writeSectionRotate(doc.to<JsonObject>(), role);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string storedJson(const Model& m) {
  JsonDocument doc;
  config_codec::toStoredJson(doc.to<JsonObject>(), m.lamp, m.base, m.shade, m.homeMode);
  std::string out;
  serializeJson(doc, out);
  return out;
}

const ColorRotation kOddRotation{true, 77, RotationDir::Down};
}  // namespace

void test_rotation_period_reads_a_32_bit_integer_and_clamps() {
  struct Case {
    const char* json;
    uint8_t expected;
  };
  const Case cases[] = {
      {"{\"periodS\":0}", 5},       {"{\"periodS\":-1}", 5},
      {"{\"periodS\":4}", 5},       {"{\"periodS\":5}", 5},
      {"{\"periodS\":120}", 120},   {"{\"periodS\":121}", 120},
      {"{\"periodS\":266}", 120},   {"{\"periodS\":70000}", 120},
      {"{\"periodS\":40.5}", 77},   {"{\"periodS\":\"40\"}", 77},
      {"{\"periodS\":true}", 77},   {"{\"periodS\":1099511627776}", 77},
      {"{\"periodS\":null}", 77},   {"{}", 77},
  };
  for (const Case& c : cases) {
    const ColorRotation r = parsedOver(c.json, kOddRotation);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(c.expected, r.periodS, c.json);
    TEST_ASSERT_TRUE_MESSAGE(r.on, c.json);
    TEST_ASSERT_TRUE_MESSAGE(r.dir == RotationDir::Down, c.json);
  }
}

void test_rotation_dir_reads_up_and_down_only() {
  for (RotationDir start : {RotationDir::Up, RotationDir::Down}) {
    const ColorRotation from{false, 40, start};
    TEST_ASSERT_TRUE(parsedOver("{\"dir\":\"up\"}", from).dir == RotationDir::Up);
    TEST_ASSERT_TRUE(parsedOver("{\"dir\":\"down\"}", from).dir == RotationDir::Down);
    for (const char* json : {"{\"dir\":\"UP\"}", "{\"dir\":\"left\"}", "{\"dir\":1}",
                             "{\"dir\":null}", "{\"dir\":\"\"}"}) {
      TEST_ASSERT_TRUE_MESSAGE(parsedOver(json, from).dir == start, json);
    }
  }
}

void test_rotation_on_takes_booleans_only() {
  for (bool start : {false, true}) {
    const ColorRotation from{start, 40, RotationDir::Up};
    TEST_ASSERT_TRUE(parsedOver("{\"on\":true}", from).on);
    TEST_ASSERT_FALSE(parsedOver("{\"on\":false}", from).on);
    for (const char* json : {"{\"on\":1}", "{\"on\":0}", "{\"on\":\"true\"}", "{\"on\":null}"}) {
      TEST_ASSERT_EQUAL_MESSAGE(start, parsedOver(json, from).on, json);
    }
  }
}

// The defaults per role: shade Down, base Up, both off at 40 s.
void test_rotation_defaults_per_role() {
  assertRotation(false, 40, RotationDir::Down, ShadeSettings{}.rotation);
  assertRotation(false, 40, RotationDir::Up, BaseSettings{}.rotation);
  assertRotation(false, 40, RotationDir::Down, parsedOver("{}", ShadeSettings{}.rotation));
  assertRotation(false, 40, RotationDir::Up, parsedOver("{}", BaseSettings{}.rotation));
  assertRotation(true, 40, RotationDir::Down,
                 parsedOver("{\"on\":true}", ShadeSettings{}.rotation));
  assertRotation(true, 40, RotationDir::Up, parsedOver("{\"on\":true}", BaseSettings{}.rotation));
}

// A stored config with no `rotate`, or one with no `dir`, loads each role's
// own default, whatever the model held before.
void test_stored_rotation_absent_keys_read_the_role_default() {
  Model m;
  m.shade.rotation = {true, 7, RotationDir::Up};
  m.base.rotation = {true, 9, RotationDir::Down};
  parseInto("{}", m);
  assertRotation(false, 40, RotationDir::Down, m.shade.rotation);
  assertRotation(false, 40, RotationDir::Up, m.base.rotation);

  m.shade.rotation = {false, 7, RotationDir::Up};
  m.base.rotation = {false, 9, RotationDir::Down};
  parseInto("{\"shade\":{\"rotate\":{\"on\":true}},\"base\":{\"rotate\":{\"on\":true}}}", m);
  assertRotation(true, 40, RotationDir::Down, m.shade.rotation);
  assertRotation(true, 40, RotationDir::Up, m.base.rotation);
}

// The segment count alone decides what loads. The flag plays no part.
void test_stored_rotation_kept_on_one_segment_and_dropped_on_two() {
  const char* oneSegment =
      "{\"shade\":{\"segments\":[{\"name\":\"Shade\",\"px\":38,\"colors\":[\"#ff000000\","
      "\"#0000ff00\"]}],\"rotate\":{\"on\":true,\"periodS\":12,\"dir\":\"up\"}},"
      "\"base\":{\"segments\":[{\"name\":\"Base\",\"px\":35,\"colors\":[\"#ff000000\"]}],"
      "\"rotate\":{\"on\":true,\"periodS\":13,\"dir\":\"down\"}}}";
  for (bool flag : {false, true}) {
    Model m;
    m.shade.rotationSupported = m.base.rotationSupported = flag;
    parseInto(oneSegment, m);
    assertRotation(true, 12, RotationDir::Up, m.shade.rotation);
    assertRotation(true, 13, RotationDir::Down, m.base.rotation);
  }
  const char* twoSegments =
      "{\"shade\":{\"segments\":[{\"name\":\"A\",\"px\":10,\"colors\":[\"#ff000000\"]},"
      "{\"name\":\"B\",\"px\":10,\"colors\":[\"#00ff0000\"]}],"
      "\"rotate\":{\"on\":true,\"periodS\":12,\"dir\":\"up\"}},"
      "\"base\":{\"segments\":[{\"name\":\"A\",\"px\":10,\"colors\":[\"#ff000000\"]},"
      "{\"name\":\"B\",\"px\":10,\"colors\":[\"#00ff0000\"]}],"
      "\"rotate\":{\"on\":true,\"periodS\":13,\"dir\":\"down\"}}}";
  Model m;
  m.shade.rotationSupported = m.base.rotationSupported = true;
  parseInto(twoSegments, m);
  assertRotation(false, 40, RotationDir::Down, m.shade.rotation);
  assertRotation(false, 40, RotationDir::Up, m.base.rotation);
}

void test_rotation_save_load_round_trip() {
  Model a;
  a.shade.rotation = {true, 5, RotationDir::Up};
  a.base.rotation = {true, 120, RotationDir::Down};
  JsonDocument out;
  config_codec::toJson(out.to<JsonObject>(), a.lamp, a.base, a.shade, a.expressions, a.homeMode);
  std::string serialized;
  serializeJson(out, serialized);
  Model b;
  parseInto(serialized.c_str(), b);
  assertRotation(true, 5, RotationDir::Up, b.shade.rotation);
  assertRotation(true, 120, RotationDir::Down, b.base.rotation);
}

// A shade stored off, 40 s, Up differs from its own default, so it writes
// `rotate` and loads back Up.
void test_shade_up_at_otherwise_default_round_trips() {
  Model a;
  a.shade.rotation = {false, 40, RotationDir::Up};
  a.base.rotation = BaseSettings::kDefaultRotation;
  const std::string json = storedJson(a);
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(),
                              "\"shade\":{\"colorsEditable\":true,\"segments\":[{\"name\":\"Shade\","
                              "\"px\":0,\"colors\":[\"#000000ff\"]}],\"rotate\":{\"on\":false,"
                              "\"periodS\":40,\"dir\":\"up\"}}"));
  TEST_ASSERT_EQUAL(json.find("\"rotate\""), json.rfind("\"rotate\""));
  Model b;
  parseInto(json.c_str(), b);
  assertRotation(false, 40, RotationDir::Up, b.shade.rotation);
}

// The section carries `rotate`, every key, exactly where the role offers
// rotation, so it never reports what the lamp will not render.
void test_section_rotate_only_where_the_role_offers_it() {
  ShadeSettings shade;
  BaseSettings base;
  shade.rotationSupported = base.rotationSupported = true;
  TEST_ASSERT_EQUAL_STRING("{\"rotate\":{\"on\":false,\"periodS\":40,\"dir\":\"down\"}}",
                           sectionRotate(shade).c_str());
  TEST_ASSERT_EQUAL_STRING("{\"rotate\":{\"on\":false,\"periodS\":40,\"dir\":\"up\"}}",
                           sectionRotate(base).c_str());
  shade.rotation = {true, 9, RotationDir::Up};
  TEST_ASSERT_EQUAL_STRING("{\"rotate\":{\"on\":true,\"periodS\":9,\"dir\":\"up\"}}",
                           sectionRotate(shade).c_str());

  shade.rotationSupported = false;
  TEST_ASSERT_FALSE(shade.offersRotation());
  TEST_ASSERT_EQUAL_STRING("{}", sectionRotate(shade).c_str());

  base.rotation = {true, 9, RotationDir::Down};
  base.segments.push_back({"Lions", 10, {Color(1, 2, 3, 4)}});
  TEST_ASSERT_FALSE(base.offersRotation());
  TEST_ASSERT_EQUAL_STRING("{}", sectionRotate(base).c_str());
}

// Scene writers read renderedRotation: the stored rotation where the role
// offers it, the role's default otherwise.
void test_rendered_rotation_is_gated_by_flag_and_segments() {
  ShadeSettings shade;
  shade.rotation = {true, 9, RotationDir::Up};
  shade.rotationSupported = true;
  assertRotation(true, 9, RotationDir::Up, shade.renderedRotation());
  shade.rotationSupported = false;
  assertRotation(false, 40, RotationDir::Down, shade.renderedRotation());
  BaseSettings base;
  base.rotation = {true, 9, RotationDir::Down};
  base.rotationSupported = true;
  assertRotation(true, 9, RotationDir::Down, base.renderedRotation());
  base.rotationSupported = false;
  assertRotation(false, 40, RotationDir::Up, base.renderedRotation());
  base.rotationSupported = true;
  base.segments.push_back({"Lions", 10, {Color(1, 2, 3, 4)}});
  assertRotation(false, 40, RotationDir::Up, base.renderedRotation());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_blob_uses_class_defaults);
  RUN_TEST(test_name_stored_lowercase);
  RUN_TEST(test_apbootminutes_defaults_and_round_trip);
  RUN_TEST(test_brightnessceiling_defaults_and_round_trip);
  RUN_TEST(test_socialmode_out_of_range_falls_back_to_ambivert);
  RUN_TEST(test_three_segment_shade_round_trip);
  RUN_TEST(test_sum_px_clamped_to_255);
  RUN_TEST(test_homemode_enabled_migration);
  RUN_TEST(test_round_trip_preserves_fields);
  RUN_TEST(test_expressions_within_caps_load_whole);
  RUN_TEST(test_over_cap_stored_expressions_truncate_on_load);
  RUN_TEST(test_stored_out_of_range_target_kept);
  RUN_TEST(test_unknown_param_keys_dropped);
  RUN_TEST(test_repeated_expression_loads_once);
  RUN_TEST(test_parse_expression_upsert_defaults_enabled);
  RUN_TEST(test_old_format_base_colors_migrated);
  RUN_TEST(test_old_format_shade_colors_migrated);
  RUN_TEST(test_old_format_no_colors_uses_class_default);
  RUN_TEST(test_new_format_segments_unchanged);
  RUN_TEST(test_named_segment_not_overwritten_by_role_default);
  RUN_TEST(test_networkbound_absent_with_ssid);
  RUN_TEST(test_networkbound_absent_no_ssid);
  RUN_TEST(test_networkbound_explicit_false_wins);
  RUN_TEST(test_disabled_expr_types_absent_defaults_to_glitchy);
  RUN_TEST(test_disabled_expr_types_explicit);
  RUN_TEST(test_disabled_expr_types_empty_array_preserved);
  RUN_TEST(test_disabled_expr_types_keep_only_ids);
  RUN_TEST(test_social_disabled_absent_defaults_true);
  RUN_TEST(test_social_disabled_explicit_false);
  RUN_TEST(test_homemode_round_trip_new_fields);
  RUN_TEST(test_rotation_period_reads_a_32_bit_integer_and_clamps);
  RUN_TEST(test_rotation_dir_reads_up_and_down_only);
  RUN_TEST(test_rotation_on_takes_booleans_only);
  RUN_TEST(test_rotation_defaults_per_role);
  RUN_TEST(test_stored_rotation_absent_keys_read_the_role_default);
  RUN_TEST(test_stored_rotation_kept_on_one_segment_and_dropped_on_two);
  RUN_TEST(test_rotation_save_load_round_trip);
  RUN_TEST(test_shade_up_at_otherwise_default_round_trips);
  RUN_TEST(test_section_rotate_only_where_the_role_offers_it);
  RUN_TEST(test_rendered_rotation_is_gated_by_flag_and_segments);
  return UNITY_END();
}
