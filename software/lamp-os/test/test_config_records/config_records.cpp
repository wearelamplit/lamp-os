// Native tests for the config storage: whole as one "cfg" string, or split
// into a compact record per expression, the knockout levels and the rest as
// blobs beside a lamp-only "cfg" string. Pins the
// round trip, the migration from the single-JSON form, a failed write, and the
// NVS budget at the caps (docs/dev/embedded-heap.md shows the arithmetic).

#include <unity.h>

#include <ArduinoJson.h>

#include <algorithm>
#include <string>
#include <vector>

#include "config/config_codec.hpp"
#include "config/disposition_store.hpp"
#include "core/boot_history.hpp"
#include "core/crash_record.hpp"
#include "paged_nvs.hpp"

#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;
using config_codec::SaveResult;

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

ExpressionConfig fullExpression(const char* type, uint8_t target) {
  ExpressionConfig e;
  e.type = type;
  e.enabled = true;
  e.target = target;
  e.intervalMin = 0xFFFFFFFFu;
  e.intervalMax = 0xFFFFFFFFu;
  for (size_t i = 0; i < kMaxExpressionColors; ++i) {
    e.colors.push_back(Color(static_cast<uint8_t>(i), 2, 3, 4));
  }
  for (size_t i = 0; i < kMaxExpressionParams; ++i) {
    e.parameters.set(kExpressionParamKeys[i], 0xFFFFFFFFu);
  }
  return e;
}

// Every capped field at its ceiling; `fill` is the character the free-text
// fields are made of, so a '"' or '\\' fill gives the fully escaped worst case.
Model worstCaseModel(char fill = 'z') {
  Model m;
  m.lamp.name = std::string(kMaxLampNameLen, fill);
  m.lamp.password = std::string(kMaxPasswordLen, fill);
  // Every number at its widest and every boolean "false", the longer form.
  m.lamp.brightness = 255;
  m.lamp.apBootMinutes = 255;
  m.lamp.brightnessCeiling = 255;
  m.lamp.socialMode = SocialMode::Extrovert;
  m.lamp.setup = m.lamp.named = m.lamp.advancedEnabled = m.lamp.webappEnabled = false;
  m.base.colorsEditable = m.shade.colorsEditable = false;
  m.homeMode.brightness = 255;
  m.homeMode.enabled = m.homeMode.networkBound = m.homeMode.socialDisabled = false;
  m.lamp.lampType = "standard";
  for (auto* segs : {&m.base.segments, &m.shade.segments}) {
    segs->clear();
    for (size_t s = 0; s < kMaxSegments; ++s) {
      SegmentSettings seg;
      seg.name = std::string(kMaxSegmentNameLen, fill);
      seg.px = 255;
      seg.colors.assign(kMaxSurfaceColors, Color(255, 255, 255, 255));
      segs->push_back(seg);
    }
  }
  m.base.byteOrder = "GRBW";
  m.shade.byteOrder = "GRBW";
  m.base.knockoutPixels.assign(255, 100);
  m.homeMode.ssid = std::string(kMaxSsidLen, fill);
  m.homeMode.disabledExpressionTypes.assign(kMaxDisabledExpressionTypes,
                                            std::string(kMaxExpressionTypeLen, 't'));
  for (size_t i = 0; i < kMaxExpressions; ++i) {
    m.expressions.expressions.push_back(fullExpression(
        std::string(kMaxExpressionTypeLen, 'a' + i % 26).c_str(), 1 + i % 3));
  }
  return m;
}

SaveResult save(ConfigStore& store, const Model& m) {
  static char cfg[config_codec::kMaxStoredCfgJsonLen + 1];
  return config_codec::saveConfig(store, m.lamp, m.base, m.shade, m.expressions,
                                  m.homeMode, cfg, sizeof(cfg));
}

// The same load Config's constructor runs.
void load(ConfigStore& store, Model& m) {
  JsonDocument doc;
  deserializeJson(doc, store.read("cfg", "{}"));
  bool withinCaps = true;
  config_codec::loadStoredConfig(store, doc.as<JsonObject>(), m.lamp, m.base, m.shade,
                                 m.expressions, m.homeMode, withinCaps);
}

// Adds full expressions until the whole config cannot fit one cfg JSON, so the
// save splits into records.
void padToSplit(Model& m) {
  for (int i = 0; i < 4; ++i) {
    m.expressions.expressions.push_back(
        fullExpression(("pad" + std::to_string(i)).c_str(), 1));
  }
}

// NVS stores 32-byte entries, 126 to a 4 KB page. A string is a header entry
// plus its bytes (with the NUL); a blob adds an index entry to that.
size_t stringEntries(size_t len) { return 1 + (len + 1 + 31) / 32; }
size_t blobEntries(size_t len) { return 2 + (len + 31) / 32; }

}  // namespace

void test_record_round_trip() {
  ExpressionConfig in = fullExpression("shifty", 2);
  uint8_t buf[config_codec::kMaxExpressionRecordLen];
  const size_t len = config_codec::encodeExpression(in, buf);
  TEST_ASSERT_EQUAL(4 + 6 + 8 + 1 + 32 + 1 + 5 * kMaxExpressionParams, len);
  ExpressionConfig out;
  TEST_ASSERT_TRUE(config_codec::decodeExpression(buf, len, out));
  TEST_ASSERT_EQUAL_STRING("shifty", out.type.c_str());
  TEST_ASSERT_TRUE(out.enabled);
  TEST_ASSERT_EQUAL_UINT8(2, out.target);
  TEST_ASSERT_EQUAL_UINT32(in.intervalMin, out.intervalMin);
  TEST_ASSERT_TRUE(in.colors == out.colors);
  TEST_ASSERT_TRUE(in.parameters == out.parameters);
}

void test_worst_record_fits_the_bound() {
  ExpressionConfig in = fullExpression("aaaaaaaaaaaaaaa", 3);
  uint8_t buf[config_codec::kMaxExpressionRecordLen];
  TEST_ASSERT_EQUAL(config_codec::kMaxExpressionRecordLen,
                    config_codec::encodeExpression(in, buf));
}

// A config can hold every key the table has; the record keeps only
// kMaxExpressionParams of them, so it never outgrows its bound.
void test_record_caps_parameters() {
  ExpressionConfig e = fullExpression("pulse", 1);
  for (size_t i = 0; i < kExpressionParamKeyCount; ++i) e.parameters.setIndex(i, 7);
  TEST_ASSERT_TRUE(e.parameters.size() > kMaxExpressionParams);
  uint8_t buf[config_codec::kMaxExpressionRecordLen + 64];
  const size_t len = config_codec::encodeExpression(e, buf);
  TEST_ASSERT_TRUE(len <= config_codec::kMaxExpressionRecordLen);
  ExpressionConfig out;
  TEST_ASSERT_TRUE(config_codec::decodeExpression(buf, len, out));
  TEST_ASSERT_EQUAL(kMaxExpressionParams, out.parameters.size());
}

void test_truncated_record_rejected() {
  ExpressionConfig in = fullExpression("pulse", 1);
  uint8_t buf[config_codec::kMaxExpressionRecordLen];
  const size_t len = config_codec::encodeExpression(in, buf);
  ExpressionConfig out;
  for (size_t cut = 0; cut < len; ++cut) {
    TEST_ASSERT_FALSE(config_codec::decodeExpression(buf, cut, out));
  }
}

void test_save_load_round_trip() {
  InMemoryConfigStore store;
  Model m = worstCaseModel();
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  Model back;
  load(store, back);
  TEST_ASSERT_EQUAL(m.expressions.expressions.size(), back.expressions.expressions.size());
  for (size_t i = 0; i < m.expressions.expressions.size(); ++i) {
    const auto& a = m.expressions.expressions[i];
    const auto& b = back.expressions.expressions[i];
    TEST_ASSERT_EQUAL_STRING(a.type.c_str(), b.type.c_str());
    TEST_ASSERT_TRUE(a.parameters == b.parameters);
    TEST_ASSERT_TRUE(a.colors == b.colors);
  }
  TEST_ASSERT_TRUE(m.base.knockoutPixels == back.base.knockoutPixels);
  TEST_ASSERT_EQUAL_STRING(m.lamp.password.c_str(), back.lamp.password.c_str());
  TEST_ASSERT_FALSE(store.read("cfg", "").find("expressions") != std::string::npos);
  TEST_ASSERT_FALSE(store.read("cfg", "").find("knockout") != std::string::npos);
}

// A lamp updating from the single-JSON form keeps its expressions and
// knockout, and a config that fits stays in that form.
void test_legacy_single_json_loads_and_small_config_stays_whole() {
  InMemoryConfigStore store;
  store.write("cfg",
              "{\"lamp\":{\"name\":\"jacko\"},\"base\":{\"segments\":[{\"name\":\"Base\","
              "\"px\":3,\"colors\":[\"#11223300\"]}],\"knockout\":[10,20,30]},"
              "\"expressions\":[{\"type\":\"pulse\",\"enabled\":true,\"target\":1,"
              "\"size\":4,\"colors\":[\"#ff000000\"]}]}");
  Model m;
  load(store, m);
  TEST_ASSERT_EQUAL(1, m.expressions.expressions.size());
  TEST_ASSERT_EQUAL_UINT8(20, m.base.knockoutPixels[1]);
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  TEST_ASSERT_FALSE(store.has("xn") || store.has("x0") || store.has("ko"));
  Model back;
  load(store, back);
  TEST_ASSERT_EQUAL(1, back.expressions.expressions.size());
  TEST_ASSERT_EQUAL_UINT32(4, back.expressions.expressions[0].getParameter("size", 0));
  TEST_ASSERT_EQUAL_UINT8(30, back.base.knockoutPixels[2]);
}

// A config too big for one cfg JSON splits into records; once it shrinks
// back, the whole save erases the split keys.
void test_split_then_whole_erases_split_keys() {
  InMemoryConfigStore store;
  Model m;
  m.base.segments = {{"Base", 3, {Color(1, 2, 3, 4)}}};
  m.base.knockoutPixels = {10, 20, 30};
  padToSplit(m);
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  TEST_ASSERT_TRUE(store.has("xn") && store.has("x3") && store.has("ko"));
  TEST_ASSERT_TRUE(store.has("cfgb") || store.has("cfgc"));
  TEST_ASSERT_TRUE(store.read("cfg", "").find("expressions") == std::string::npos);
  m.expressions.expressions.resize(1);
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  TEST_ASSERT_FALSE(store.has("xn") || store.has("x0") || store.has("ko"));
  TEST_ASSERT_FALSE(store.has("cfgb") || store.has("cfgc") || store.has("cfgl"));
  Model back;
  load(store, back);
  TEST_ASSERT_EQUAL(1, back.expressions.expressions.size());
  TEST_ASSERT_EQUAL_UINT8(20, back.base.knockoutPixels[1]);
}

// A lamp flashed back to a build that predates records rewrites the cfg JSON
// with its own expressions and knockout; flashed forward again, that config
// loads and the stale records and knockout blob are ignored.
void test_config_saved_by_older_firmware_wins_over_stale_records() {
  InMemoryConfigStore store;
  Model m;
  m.base.segments = {{"Base", 3, {Color(1, 2, 3, 4)}}};
  m.base.knockoutPixels = {10, 20, 30};
  padToSplit(m);
  save(store, m);
  store.write("cfg",
              "{\"lamp\":{\"name\":\"jacko\"},\"base\":{\"segments\":[{\"name\":\"Base\","
              "\"px\":3,\"colors\":[\"#11223300\"]}],\"knockout\":[1,2,3]},"
              "\"expressions\":[{\"type\":\"spotty\",\"enabled\":true,\"target\":2}]}");
  Model back;
  load(store, back);
  TEST_ASSERT_EQUAL(1, back.expressions.expressions.size());
  TEST_ASSERT_EQUAL_STRING("spotty", back.expressions.expressions[0].type.c_str());
  TEST_ASSERT_EQUAL_UINT8(2, back.base.knockoutPixels[1]);
  TEST_ASSERT_TRUE(save(store, back) == SaveResult::Ok);
  TEST_ASSERT_FALSE(store.has("xn") || store.has("x0") || store.has("ko"));
}

// A cfg JSON that cannot fit is refused before any record, count or
// knockout write.
void test_too_large_writes_nothing() {
  InMemoryConfigStore store;
  Model m;
  padToSplit(m);
  char small[64];
  TEST_ASSERT_TRUE(config_codec::saveConfig(store, m.lamp, m.base, m.shade, m.expressions,
                                            m.homeMode, small, sizeof(small)) ==
                   SaveResult::TooLarge);
  TEST_ASSERT_EQUAL(0, store.writeCount);
}

// Over-long fields arriving through the cfg JSON (a web save or an old
// store) are bounded at parse, so the save that follows always fits.
void test_over_long_fields_cannot_fail_every_save() {
  const std::string big(5000, 'z');
  const std::string json =
      "{\"lamp\":{\"name\":\"" + big + "\",\"password\":\"" + big +
      "\"},\"base\":{\"byteOrder\":\"" + big + "\",\"segments\":[{\"name\":\"" + big +
      "\",\"px\":3,\"colors\":[\"#11223300\"]}]},\"shade\":{\"byteOrder\":\"" + big +
      "\"},\"homeMode\":{\"ssid\":\"" + big + "\",\"disabledExpressionTypes\":[\"" + big +
      "\"]}}";
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
  Model m;
  config_codec::fromJson(doc.as<JsonObject>(), m.lamp, m.base, m.shade, m.expressions,
                         m.homeMode);
  TEST_ASSERT_EQUAL(kMaxPasswordLen, m.lamp.password.size());
  TEST_ASSERT_EQUAL(kMaxSsidLen, m.homeMode.ssid.size());
  TEST_ASSERT_TRUE(m.base.byteOrder.empty());
  TEST_ASSERT_TRUE(m.shade.byteOrder.empty());
  InMemoryConfigStore store;
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
}

void test_removed_expressions_erase_stale_records() {
  InMemoryConfigStore store;
  Model m;
  m.expressions.expressions = {fullExpression("pulse", 1), fullExpression("shifty", 2)};
  save(store, m);
  m.expressions.expressions.pop_back();
  save(store, m);
  TEST_ASSERT_FALSE(store.has("x1"));
  Model back;
  load(store, back);
  TEST_ASSERT_EQUAL(1, back.expressions.expressions.size());
}

// A full NVS returns a write failure, never a fault, and leaves the stored
// config readable.
void test_failed_write_reports_and_keeps_the_old_config() {
  InMemoryConfigStore store;
  Model m;
  m.lamp.name = "jacko";
  m.expressions.expressions = {fullExpression("pulse", 1)};
  save(store, m);
  store.failWrites = true;
  m.lamp.name = "other";
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::WriteFailed);
  Model back;
  load(store, back);
  TEST_ASSERT_EQUAL_STRING("jacko", back.lamp.name.c_str());
}

namespace {

std::vector<std::pair<std::string, uint8_t>> keysOf(const ExpressionSettings& s) {
  std::vector<std::pair<std::string, uint8_t>> keys;
  for (const auto& e : s.expressions) keys.push_back({e.type, e.target});
  std::sort(keys.begin(), keys.end());
  return keys;
}

ExpressionConfig tagged(const char* type, uint8_t target, uint32_t size) {
  ExpressionConfig e;
  e.type = type;
  e.target = target;
  e.colors = {Color(1, 2, 3, 4)};
  e.setParameter("size", size);
  return e;
}

// Saves `before`, then saves `after` with the store reset partway through,
// at every possible write; each load must be a whole config: the lamp section
// and the rest each as one of the two saved them, every entry is one `before`
// or `after` held (same type, target and contents), every entry both hold is
// present, and nothing from neither appears.
void assertCrashSafe(Model before, Model after) {
  before.lamp.name = "before";
  before.lamp.brightness = 1;
  after.lamp.name = "after";
  after.lamp.brightness = 2;
  after.homeMode.ssid = "after";
  for (size_t crashAt = 0; crashAt < 64; ++crashAt) {
    InMemoryConfigStore store;
    save(store, before);
    store.writesBeforeCrash = crashAt;
    const bool finished = save(store, after) == SaveResult::Ok;
    store.writesBeforeCrash = SIZE_MAX;
    Model back;
    load(store, back);
    for (const Model* m : {&before, &after}) {
      if (back.lamp.name == m->lamp.name) {
        TEST_ASSERT_EQUAL_UINT8(m->lamp.brightness, back.lamp.brightness);
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(back.lamp.name == "before" || back.lamp.name == "after",
                             "lamp section lost");
    TEST_ASSERT_TRUE_MESSAGE(back.homeMode.ssid == before.homeMode.ssid ||
                                 back.homeMode.ssid == "after",
                             "rest lost");
    auto keys = keysOf(back.expressions);
    TEST_ASSERT_TRUE_MESSAGE(std::adjacent_find(keys.begin(), keys.end()) == keys.end(),
                             "an expression loaded twice");
    for (const auto& e : back.expressions.expressions) {
      auto matches = [&](const Model& m) {
        for (const auto& o : m.expressions.expressions) {
          if (o.type == e.type && o.target == e.target && o.parameters == e.parameters) return true;
        }
        return false;
      };
      TEST_ASSERT_TRUE_MESSAGE(matches(before) || matches(after), e.type.c_str());
    }
    for (const auto& o : before.expressions.expressions) {
      bool inAfter = false;
      for (const auto& a : after.expressions.expressions) {
        inAfter |= a.type == o.type && a.target == o.target;
      }
      if (!inAfter) continue;
      bool loaded = false;
      for (const auto& e : back.expressions.expressions) {
        loaded |= e.type == o.type && e.target == o.target;
      }
      TEST_ASSERT_TRUE_MESSAGE(loaded, o.type.c_str());
    }
    if (finished) {
      TEST_ASSERT_TRUE(keysOf(back.expressions) == keysOf(after.expressions));
      return;
    }
  }
  TEST_FAIL_MESSAGE("save never finished");
}

}  // namespace

void test_crash_mid_save_removing_an_entry_keeps_the_rest() {
  for (bool split : {false, true}) {
    Model before;
    before.expressions.expressions = {tagged("pulse", 1, 1), tagged("shifty", 2, 2),
                                      tagged("spotty", 3, 3), tagged("glitchy", 1, 4)};
    if (split) padToSplit(before);
    Model after = before;
    after.expressions.expressions.erase(after.expressions.expressions.begin());
    assertCrashSafe(before, after);
  }
}

void test_crash_mid_save_adding_and_editing_keeps_the_rest() {
  for (bool split : {false, true}) {
    Model before;
    before.expressions.expressions = {tagged("pulse", 1, 1), tagged("shifty", 2, 2)};
    if (split) padToSplit(before);
    Model after = before;
    after.expressions.expressions[1] = tagged("shifty", 2, 9);
    after.expressions.expressions.push_back(tagged("spotty", 3, 3));
    assertCrashSafe(before, after);
  }
}

// Growing past one cfg JSON, or shrinking back under it, is reset-safe too.
void test_crash_mid_save_crossing_the_split_keeps_the_rest() {
  Model small;
  small.expressions.expressions = {tagged("pulse", 1, 1), tagged("shifty", 2, 2)};
  Model big = small;
  padToSplit(big);
  assertCrashSafe(small, big);
  assertCrashSafe(big, small);
}

// A reset during the first save after an update leaves the single-JSON form
// readable: the records only take over once their count is written, and the
// legacy JSON is rewritten last.
void test_crash_mid_migration_keeps_the_legacy_config() {
  for (size_t crashAt = 0; crashAt < 64; ++crashAt) {
    InMemoryConfigStore store;
    store.write("cfg",
                "{\"lamp\":{\"name\":\"jacko\"},\"expressions\":["
                "{\"type\":\"pulse\",\"enabled\":true,\"target\":1,\"size\":4},"
                "{\"type\":\"shifty\",\"enabled\":true,\"target\":2}]}");
    Model m;
    load(store, m);
    store.writesBeforeCrash = crashAt;
    const bool finished = save(store, m) == SaveResult::Ok;
    store.writesBeforeCrash = SIZE_MAX;
    Model back;
    load(store, back);
    TEST_ASSERT_EQUAL_STRING("jacko", back.lamp.name.c_str());
    TEST_ASSERT_EQUAL(2, back.expressions.expressions.size());
    if (finished) return;
  }
  TEST_FAIL_MESSAGE("save never finished");
}

// An unreadable record drops itself and nothing else.
// A cfg JSON that outgrows the fixed save buffer fails the save and leaves
// the stored cfg as it was.
void test_cfg_json_over_the_buffer_fails_cleanly() {
  InMemoryConfigStore store;
  Model m;
  m.lamp.name = "jacko";
  save(store, m);
  const std::string before = store.read("cfg", "");
  char small[64];
  m.lamp.name = "other";
  TEST_ASSERT_TRUE(config_codec::saveConfig(store, m.lamp, m.base, m.shade, m.expressions,
                                            m.homeMode, small, sizeof(small)) ==
                   SaveResult::TooLarge);
  TEST_ASSERT_EQUAL_STRING(before.c_str(), store.read("cfg", "").c_str());
}

bool anyRecordLeft(InMemoryConfigStore& store) {
  for (int i = 0; i < 32; ++i) {
    if (store.has(("x" + std::to_string(i)).c_str())) return true;
  }
  return false;
}

// A whole save reset at any write, then a later whole save, leaves no record
// behind: the records go before the count, and every slot is swept.
void test_no_record_is_orphaned_by_a_reset() {
  Model big;
  padToSplit(big);
  big.expressions.expressions.push_back(tagged("pulse", 1, 1));
  Model small;
  small.expressions.expressions = {tagged("shifty", 2, 2)};
  for (size_t crashAt = 0; crashAt < 40; ++crashAt) {
    InMemoryConfigStore store;
    save(store, big);
    store.writesBeforeCrash = crashAt;
    save(store, small);
    store.writesBeforeCrash = SIZE_MAX;
    TEST_ASSERT_TRUE(save(store, small) == SaveResult::Ok);
    TEST_ASSERT_FALSE(anyRecordLeft(store));
    TEST_ASSERT_FALSE(store.has("xn"));
  }
}

// A split save cut short before its count leaves records past the old
// count; the next split save sweeps them.
void test_split_save_sweeps_records_past_its_count() {
  InMemoryConfigStore store;
  Model m;
  padToSplit(m);
  padToSplit(m);  // 8 full expressions: every save below stays split
  save(store, m);
  Model grown = m;
  for (int i = 0; i < 4; ++i) grown.expressions.expressions.push_back(tagged("spotty", 1 + i % 3, i));
  store.writesBeforeCrash = 11;  // records x0..x10 land, x11 and the count do not
  save(store, grown);
  store.writesBeforeCrash = SIZE_MAX;
  TEST_ASSERT_TRUE(store.has("x10"));
  Model shrunk = m;
  shrunk.expressions.expressions.pop_back();
  TEST_ASSERT_TRUE(save(store, shrunk) == SaveResult::Ok);
  TEST_ASSERT_TRUE(store.has("xn"));
  TEST_ASSERT_TRUE(store.has("x6"));
  for (int i = 7; i <= 10; ++i) {
    TEST_ASSERT_FALSE_MESSAGE(store.has(("x" + std::to_string(i)).c_str()),
                              std::to_string(i).c_str());
  }
}

// A record slot past every count this build writes (an earlier build's cap
// was higher) is swept by a whole save and by a split save alike.
void test_saves_sweep_record_slots_past_the_cap() {
  const uint8_t junk[3] = {1, 2, 3};
  for (bool split : {false, true}) {
    InMemoryConfigStore store;
    store.writeBlob("x25", junk, sizeof(junk));
    Model m;
    if (split) padToSplit(m);
    TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
    TEST_ASSERT_EQUAL(split, store.has("xn"));
    TEST_ASSERT_FALSE(store.has("x25"));
  }
}

// Deleting every expression saves whole with an empty expressions array; a
// reset before the old records are erased must still load none, never the
// records the delete removed.
void test_delete_all_survives_a_reset_before_the_record_erase() {
  Model before;
  padToSplit(before);
  Model none;
  for (size_t crashAt = 1; crashAt < 40; ++crashAt) {
    InMemoryConfigStore store;
    save(store, before);
    store.writesBeforeCrash = crashAt;
    const bool finished = save(store, none) == SaveResult::Ok;
    store.writesBeforeCrash = SIZE_MAX;
    TEST_ASSERT_TRUE(store.read("cfg", "").find("\"expressions\":[]") != std::string::npos);
    Model back;
    load(store, back);
    TEST_ASSERT_EQUAL(0, back.expressions.expressions.size());
    if (finished) return;
  }
}

// A whole cfg string is the authority, so rest and lamp blobs beside it (a
// save cut short, or left by a downgrade) go at load: a split save would
// otherwise take a rest blob for a store split already.
void test_load_of_a_whole_store_drops_stale_blobs() {
  InMemoryConfigStore store;
  Model m;
  m.lamp.name = "whole";
  save(store, m);
  store.writeBlob("cfgb", "{}", 2);
  store.writeBlob("cfgc", "{}", 2);
  store.writeBlob("cfgl", "{}", 2);
  Model back;
  load(store, back);
  TEST_ASSERT_EQUAL_STRING("whole", back.lamp.name.c_str());
  TEST_ASSERT_FALSE(store.has("cfgb") || store.has("cfgc") || store.has("cfgl"));
}

// A split config keeps the lamp section alone in the cfg string and the rest
// in a rest blob.
void test_split_keeps_the_lamp_section_alone_in_cfg() {
  InMemoryConfigStore store;
  Model m = worstCaseModel();
  m.lamp.name = "split";
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, store.read("cfg", "")) == DeserializationError::Ok);
  TEST_ASSERT_EQUAL_STRING("split", doc["lamp"]["name"] | "");
  TEST_ASSERT_EQUAL(1, doc.as<JsonObject>().size());
  TEST_ASSERT_TRUE(store.has("cfgb") != store.has("cfgc"));
  TEST_ASSERT_FALSE(store.has("cfgl"));
}

// The whole cap is kMaxWholeCfgLen: a config whose whole JSON is that long
// saves whole, and one a byte longer saves split.
void test_the_whole_cap_is_exact() {
  static char buf[config_codec::kMaxStoredCfgJsonLen + 1];
  const auto wholeLen = [&](const Model& m) {
    JsonWriter w(buf, sizeof(buf));
    config_codec::writeStoredJson(w, &m.lamp, m.base, m.shade, m.homeMode, &m.expressions);
    w.finish();
    return w.ok() ? w.length() : SIZE_MAX;
  };
  for (size_t target : {config_codec::kMaxWholeCfgLen, config_codec::kMaxWholeCfgLen + 1}) {
    bool found = false;
    // Expressions and surface colours get within the lamp name and SSID's
    // reach of the target; those two close the gap.
    for (size_t full = 0; full < 4 && !found; ++full) {
      for (size_t colors = 0; colors <= kMaxExpressionColors && !found; ++colors) {
        Model m;
        for (size_t i = 0; i < full; ++i) m.expressions.expressions.push_back(fullExpression("pulse", 1));
        ExpressionConfig partial = fullExpression("shifty", 2);
        partial.colors.resize(colors);
        m.expressions.expressions.push_back(partial);
        // Surface colours (about 12 bytes each) close most of the rest.
        m.base.segments = {{"Base", 8, {}}};
        size_t baseLen = wholeLen(m);
        while (baseLen < target && target - baseLen > kMaxLampNameLen + kMaxSsidLen &&
               m.base.segments[0].colors.size() < kMaxSurfaceColors) {
          m.base.segments[0].colors.push_back(Color(9, 9, 9, 9));
          baseLen = wholeLen(m);
        }
        if (baseLen > target || target - baseLen > kMaxLampNameLen + kMaxSsidLen) continue;
        for (size_t name = 0; name <= kMaxLampNameLen && wholeLen(m) != target; ++name) {
          m.lamp.name = std::string(name, 'n');
          for (size_t ssid = 0; ssid <= kMaxSsidLen && wholeLen(m) != target; ++ssid) {
            m.homeMode.ssid = std::string(ssid, 's');
          }
        }
        if (wholeLen(m) != target) continue;
        found = true;
        InMemoryConfigStore store;
        TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
        const bool whole = store.read("cfg", "").find("\"expressions\":") != std::string::npos;
        TEST_ASSERT_EQUAL(target == config_codec::kMaxWholeCfgLen, whole);
        TEST_ASSERT_EQUAL(!whole, store.has("xn"));
      }
    }
    TEST_ASSERT_TRUE(found);
  }
}

bool loadsUnreadable(ConfigStore& store) {
  Model back;
  JsonDocument doc;
  deserializeJson(doc, store.read("cfg", "{}"));
  bool withinCaps = true;
  return config_codec::loadStoredConfig(store, doc.as<JsonObject>(), back.lamp, back.base,
                                        back.shade, back.expressions, back.homeMode, withinCaps)
      .unreadable;
}

// A rest blob that holds something unreadable is reported, so Config serves
// defaults without letting the first-boot save write them over it: JSON that
// does not parse, a blob over the buffer, or a chunk that fails its CRC (the
// first read after it; IDF erases the chunk then).
void test_an_unreadable_rest_blob_is_reported() {
  {
    InMemoryConfigStore store;
    save(store, worstCaseModel());
    TEST_ASSERT_FALSE(loadsUnreadable(store));
    store.writeBlob("cfgb", "{broken", 7);
    TEST_ASSERT_TRUE(loadsUnreadable(store));
  }
  {
    InMemoryConfigStore store;
    save(store, worstCaseModel());
    const std::string over(config_codec::kMaxStoredCfgJsonLen + 1, ' ');
    store.writeBlob("cfgb", over.data(), over.size());
    TEST_ASSERT_TRUE(loadsUnreadable(store));
  }
  {
    PagedNvs nvs;
    save(nvs, worstCaseModel());
    nvs.corruptBlob(nvs.has("cfgb") ? "cfgb" : "cfgc");
    TEST_ASSERT_TRUE(loadsUnreadable(nvs));
  }
}

// So is a "cfgl" holding the only lamp section when a chunk fails its CRC.
void test_an_unreadable_lamp_blob_is_reported() {
  PagedNvs nvs;
  const Model m = worstCaseModel();
  save(nvs, m);
  static char lampJson[512];
  JsonWriter w(lampJson, sizeof(lampJson));
  config_codec::writeLampOnlyJson(w, m.lamp);
  w.finish();
  nvs.writeBlob("cfgl", lampJson, w.length());
  nvs.erase("cfg");
  TEST_ASSERT_FALSE(loadsUnreadable(nvs));
  nvs.corruptBlob("cfgl");
  TEST_ASSERT_TRUE(loadsUnreadable(nvs));
}


// An unreadable record drops only itself.
void test_unreadable_record_drops_only_itself() {
  InMemoryConfigStore store;
  Model m;
  m.expressions.expressions = {tagged("pulse", 1, 1), tagged("shifty", 2, 2),
                               tagged("spotty", 3, 3)};
  padToSplit(m);
  save(store, m);
  const uint8_t junk[3] = {9, 9, 9};
  store.writeBlob("x1", junk, sizeof(junk));
  Model back;
  JsonDocument doc;
  deserializeJson(doc, store.read("cfg", "{}"));
  config_codec::fromJson(doc.as<JsonObject>(), back.lamp, back.base, back.shade,
                         back.expressions, back.homeMode);
  const auto loaded = config_codec::loadRecords(store, back.base, back.expressions,
                                                false, false);
  TEST_ASSERT_TRUE(loaded.present);
  TEST_ASSERT_EQUAL_UINT32(1u << 1, loaded.droppedMask);
  TEST_ASSERT_EQUAL(6, back.expressions.expressions.size());
  TEST_ASSERT_EQUAL_STRING("pulse", back.expressions.expressions[0].type.c_str());
  TEST_ASSERT_EQUAL_STRING("spotty", back.expressions.expressions[1].type.c_str());
}

// A type the running variant does not register still loads: it may belong to
// the variant the lamp is flashed back to, and dropping it would lose it.
void test_unregistered_type_record_loads() {
  InMemoryConfigStore store;
  Model m;
  m.expressions.expressions = {tagged("bloom", 3, 1), tagged("pulse", 1, 2)};
  save(store, m);
  Model back;
  load(store, back);
  TEST_ASSERT_EQUAL(2, back.expressions.expressions.size());
}

// NVS budget against the 20 KB partition: 5 pages of 126 entries, one held
// free for garbage collection. docs/dev/embedded-heap.md shows the arithmetic.
constexpr size_t kPageEntries = PagedNvs::kEntries;
constexpr size_t kDataPages = PagedNvs::kPages - 1;
constexpr size_t kMargin = 29;

// The keys outside the lamp namespace, as measured or bounded from each
// owner's store format. `pad` adds that many one-entry keys.
// - 5 namespace entries: lamp, crash, phy, nvs.net80211, nimble_bond;
// - PHY calibration: a 1904-byte cal_data blob, a 6-byte cal_mac blob and a
//   u32 cal_version;
// - the WiFi driver's namespace: empty, erased at boot (its config is in RAM);
// - the crash namespace: the crash record and the boot history, one blob each;
// - NimBLE's bond store (ble_store_nvs.c, bonding on, privacy on): our_sec_N
//   and peer_sec_N blobs of ble_store_value_sec (88 bytes on the ESP32) for
//   each of MAX_BONDS (3), p_dev_rec_N blobs of ble_hs_dev_records (44 bytes)
//   for MAX_BONDS + 1, cccd_sec_N blobs (16 bytes) for MAX_CCCDS (8),
//   csfc_sec_N (8 bytes) for MAX_CSFCS (1, the connection count), and
//   local_irk_N and rpa_rec_N (under 32 bytes) for MAX_BONDS each.
struct SystemKey {
  std::string key;
  size_t len;
};

std::vector<SystemKey> systemKeys() {
  std::vector<SystemKey> keys;
  for (const char* ns : {"ns/lamp", "ns/crash", "ns/phy", "ns/net80211", "ns/bond"}) {
    keys.push_back({ns, 0});
  }
  keys.push_back({"phy/cal_data", 1904});
  keys.push_back({"phy/cal_mac", 6});
  keys.push_back({"phy/cal_version", 0});
  keys.push_back({"crash/last", sizeof(lamp::crash_record::Record)});
  keys.push_back({"crash/boots", sizeof(lamp::boot_history::History)});
  for (int i = 0; i < 3; ++i) {
    keys.push_back({"bond/our_sec_" + std::to_string(i), 88});
    keys.push_back({"bond/peer_sec_" + std::to_string(i), 88});
    keys.push_back({"bond/local_irk_" + std::to_string(i), 32});
    keys.push_back({"bond/rpa_rec_" + std::to_string(i), 32});
  }
  for (int i = 0; i < 4; ++i) keys.push_back({"bond/p_dev_rec_" + std::to_string(i), 44});
  for (int i = 0; i < 8; ++i) keys.push_back({"bond/cccd_sec_" + std::to_string(i), 16});
  keys.push_back({"bond/csfc_sec_0", 8});
  return keys;
}

size_t systemEntries() {
  size_t n = 0;
  for (const auto& k : systemKeys()) n += k.len == 0 ? 1 : blobEntries(k.len);
  return n;
}

// The lamp namespace's keys besides the config: lampType and dispositions.
size_t lampExtraEntries() {
  return stringEntries(16) + blobEntries(DispositionStore::kMaxBlobLen);
}

enum class Part { Lamp, Rest, Whole };

std::string savedString(const Model& m, Part part) {
  static char buf[config_codec::kMaxStoredCfgJsonLen + 1];
  JsonWriter w(buf, sizeof(buf));
  if (part == Part::Lamp) {
    config_codec::writeLampOnlyJson(w, m.lamp);
  } else {
    config_codec::writeStoredJson(w, part == Part::Whole ? &m.lamp : nullptr, m.base, m.shade,
                                  m.homeMode, part == Part::Whole ? &m.expressions : nullptr);
  }
  w.finish();
  TEST_ASSERT_TRUE(w.ok());
  return std::string(buf);
}

// A string of `span` entries is placed when some page, compacted, has that
// many free: the page with the fewest live entries holds at most a quarter of
// the `live` entries on the four data pages. Returns the live entries that
// could still be added before that guarantee breaks.
long stringPlacementMargin(size_t live, size_t span) {
  return static_cast<long>(kDataPages * (kPageEntries - span) + kDataPages - 1) -
         static_cast<long>(live);
}

size_t restBlobEntries(const Model& m) {
  return blobEntries(savedString(m, Part::Rest).size());
}

// The split keys but the rest blob and the cfg string: records, the count
// (a decimal string) and knockout.
size_t splitKeyEntries(const Model& m) {
  uint8_t buf[config_codec::kMaxExpressionRecordLen];
  size_t records = 0;
  for (const auto& e : m.expressions.expressions) {
    records += blobEntries(config_codec::encodeExpression(e, buf));
  }
  return records + stringEntries(std::to_string(m.expressions.expressions.size()).size()) +
         blobEntries(m.base.knockoutPixels.size());
}

// The save buffer holds a split save's rest, with its NUL, at
// every escaped worst case.
void test_save_buffer_holds_the_worst_rest() {
  for (char fill : {'z', '"', '\\'}) {
    const size_t len = savedString(worstCaseModel(fill), Part::Rest).size() + 1;
    printf("[budget] rest blob '%c': %zu of %zu bytes\n", fill, len,
           config_codec::kMaxStoredCfgJsonLen);
    TEST_ASSERT_TRUE(len <= config_codec::kMaxStoredCfgJsonLen);
  }
}

// The widest rest that can carry `rotate`: one four-segment role and one
// single-segment role with `rotate` at its widest, every other field at its
// cap. It stays under the four-segment worst case, so the caps stand.
void test_widest_rotating_rest_fits_under_the_worst_rest() {
  for (char fill : {'z', '"', '\\'}) {
    const size_t worst = savedString(worstCaseModel(fill), Part::Rest).size();
    for (bool singleShade : {true, false}) {
      Model m = worstCaseModel(fill);
      (singleShade ? m.shade.segments : m.base.segments).resize(1);
      (singleShade ? m.shade.rotation : m.base.rotation) = {false, 120, RotationDir::Down};
      const std::string rest = savedString(m, Part::Rest);
      TEST_ASSERT_NOT_NULL(
          strstr(rest.c_str(), "\"rotate\":{\"on\":false,\"periodS\":120,\"dir\":\"down\"}"));
      printf("[budget] rotating rest '%c' (%s single): %zu of %zu bytes\n", fill,
             singleShade ? "shade" : "base", rest.size() + 1, config_codec::kMaxStoredCfgJsonLen);
      TEST_ASSERT_TRUE(rest.size() + 1 <= config_codec::kMaxStoredCfgJsonLen);
      TEST_ASSERT_TRUE(rest.size() < worst);
    }
  }
}

// The web GET document carries a one-segment role's rotation and its PUT
// brings it back. The PUT parses into default-constructed settings, whose
// flag is off, the only way this path runs.
void test_web_document_round_trip_keeps_rotation() {
  Model m;
  m.shade.rotation = {true, 7, RotationDir::Up};
  m.base.rotation = {true, 33, RotationDir::Down};
  m.shade.rotationSupported = m.base.rotationSupported = true;
  JsonDocument doc;
  config_codec::toJson(doc.to<JsonObject>(), m.lamp, m.base, m.shade, m.expressions, m.homeMode);
  std::string body;
  serializeJson(doc, body);
  InMemoryConfigStore store;
  static char cfg[config_codec::kMaxStoredCfgJsonLen + 1];
  TEST_ASSERT_TRUE(config_codec::saveRawJson(store, body.c_str(), cfg, sizeof(cfg), "") ==
                   SaveResult::Ok);
  Model back;
  load(store, back);
  TEST_ASSERT_TRUE(back.shade.rotation == m.shade.rotation);
  TEST_ASSERT_TRUE(back.base.rotation == m.base.rotation);
}

// The whole cfg string at kMaxWholeCfgLen with everything else at its cap:
// rewritten over its old copy, or placed after a staged rest blob of the same
// size once the old string and the split keys are erased.
void test_whole_cfg_string_is_always_placed() {
  const size_t span = stringEntries(config_codec::kMaxWholeCfgLen);
  const size_t live = systemEntries() + lampExtraEntries() +
                      std::max(span, blobEntries(config_codec::kMaxWholeCfgLen));
  const long margin = stringPlacementMargin(live, span);
  printf("[budget] whole cfg: %zu entries, %zu live, margin %ld\n", span, live, margin);
  TEST_ASSERT_TRUE(margin >= static_cast<long>(kMargin));
}

// The split save's lamp string, rewritten over its old copy beside every
// split key at its cap, or placed after a staged "cfgl" copy once a whole
// string is erased.
void test_lamp_cfg_string_is_always_placed() {
  for (char fill : {'z', '"', '\\'}) {
    const Model m = worstCaseModel(fill);
    const size_t lampLen = savedString(m, Part::Lamp).size();
    const size_t span = stringEntries(lampLen);
    const size_t live = systemEntries() + lampExtraEntries() + splitKeyEntries(m) +
                        restBlobEntries(m) + std::max(span, blobEntries(lampLen));
    const long margin = stringPlacementMargin(live, span);
    printf("[budget] lamp cfg '%c': %zu entries, %zu live, margin %ld\n", fill, span, live,
           margin);
    TEST_ASSERT_TRUE(margin >= static_cast<long>(kMargin));
  }
}

// A blob under 400 bytes is placed like a string: IDF writes its one chunk
// whole and moves to a fresh page rather than split it while the tail holds
// less than 400 bytes. The largest is a disposition blob just under that
// (14 entries), rewritten beside every split key at its cap.
void test_small_blobs_are_always_placed() {
  const size_t span = 1 + (399 + 31) / 32;
  for (char fill : {'z', '"', '\\'}) {
    const Model m = worstCaseModel(fill);
    const size_t live = systemEntries() + lampExtraEntries() + splitKeyEntries(m) +
                        restBlobEntries(m) + stringEntries(savedString(m, Part::Lamp).size());
    const long margin = stringPlacementMargin(live, span);
    printf("[budget] small blob '%c': %zu entries, %zu live, margin %ld\n", fill, span, live,
           margin);
    TEST_ASSERT_TRUE(margin >= static_cast<long>(kMargin));
  }
}

// Blob writes need the new copy's entries before the old copy goes. The
// split store's peak is its largest blob rewritten, PHY calibration (1904
// bytes) or the rest; switching from whole adds every split key, the staged
// lamp section and the new rest blob beside the whole string; staging a
// whole config adds its blob beside the split store.
void test_split_peaks_fit_the_entries() {
  for (char fill : {'z', '"', '\\'}) {
    const Model m = worstCaseModel(fill);
    const size_t lampLen = savedString(m, Part::Lamp).size();
    const size_t base = systemEntries() + lampExtraEntries() + splitKeyEntries(m);
    const size_t steady =
        base + restBlobEntries(m) + stringEntries(lampLen) +
        std::max(restBlobEntries(m), blobEntries(1904));
    const size_t switching = base + stringEntries(config_codec::kMaxWholeCfgLen) +
                             blobEntries(lampLen) + restBlobEntries(m);
    const size_t staging = base + restBlobEntries(m) + stringEntries(lampLen) +
                           blobEntries(config_codec::kMaxWholeCfgLen);
    printf("[budget] split '%c': steady peak %zu, switch peak %zu, staging peak %zu of %zu\n",
           fill, steady, switching, staging, kDataPages * kPageEntries);
    for (size_t peak : {steady, switching, staging}) {
      TEST_ASSERT_TRUE(peak + kMargin <= kDataPages * kPageEntries);
    }
  }
}

// The largest whole config: full expressions until the next would pass
// kMaxWholeCfgLen.
Model largestWholeModel() {
  Model m;
  m.lamp.name = std::string(kMaxLampNameLen, 'w');
  static char buf[config_codec::kMaxWholeCfgLen + 1];
  for (size_t i = 0; i < kMaxExpressions; ++i) {
    m.expressions.expressions.push_back(fullExpression("pulse", 1 + i % 3));
    JsonWriter w(buf, sizeof(buf));
    config_codec::writeStoredJson(w, &m.lamp, m.base, m.shade, m.homeMode, &m.expressions);
    w.finish();
    if (!w.ok()) {
      m.expressions.expressions.pop_back();
      break;
    }
  }
  TEST_ASSERT_TRUE(m.expressions.expressions.size() > 1);
  return m;
}

uint32_t nextRandom(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state >> 8;
}

// The fields each storage key carries, compared after a load.
bool sameLamp(const Model& a, const Model& b) {
  return a.lamp.name == b.lamp.name && a.lamp.password == b.lamp.password &&
         a.lamp.brightness == b.lamp.brightness;
}
bool sameSegments(const std::vector<SegmentSettings>& a, const std::vector<SegmentSettings>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].name != b[i].name || a[i].px != b[i].px || a[i].colors != b[i].colors) return false;
  }
  return true;
}
bool sameRest(const Model& a, const Model& b) {
  return sameSegments(a.base.segments, b.base.segments) &&
         sameSegments(a.shade.segments, b.shade.segments) &&
         a.base.knockoutPixels == b.base.knockoutPixels && a.homeMode.ssid == b.homeMode.ssid;
}
bool sameExpressions(const Model& a, const Model& b) {
  const auto& x = a.expressions.expressions;
  const auto& y = b.expressions.expressions;
  if (x.size() != y.size()) return false;
  for (size_t i = 0; i < x.size(); ++i) {
    if (x[i].type != y[i].type || x[i].intervalMin != y[i].intervalMin) return false;
  }
  return true;
}
// Every loaded expression is one `a` or `b` holds: a save cut short rewrites
// some records and not others.
bool expressionsFrom(const Model& back, const Model& a, const Model& b) {
  for (const auto& e : back.expressions.expressions) {
    bool found = false;
    for (const Model* m : {&a, &b}) {
      for (const auto& o : m->expressions.expressions) {
        found |= o.type == e.type && o.target == e.target && o.intervalMin == e.intervalMin;
      }
    }
    if (!found) return false;
  }
  return true;
}
// Every section loads as one of the two models it may: a save cut short
// leaves the lamp section and the rest each as before or as saved, and each
// expression as one of them held it.
bool eachSectionOneOf(const Model& back, const Model& a, const Model& b) {
  return (sameLamp(back, a) || sameLamp(back, b)) && (sameRest(back, a) || sameRest(back, b)) &&
         expressionsFrom(back, a, b);
}
bool allSections(const Model& back, const Model& m) {
  return sameLamp(back, m) && sameRest(back, m) && sameExpressions(back, m);
}

// Fails writes and erases past `budget`, the way a reset mid-save leaves NVS.
// With `refuseWhole`, the first whole cfg string written is refused, the way
// NVS refuses one it has no page for, which drives a staged whole save.
class CrashAfter : public ConfigStore {
 public:
  CrashAfter(ConfigStore& inner, size_t budget, bool refuseWhole = false)
      : inner_(inner), budget_(budget), refuseWhole_(refuseWhole) {}
  std::string read(const char* key, const char* d) override { return inner_.read(key, d); }
  size_t readString(const char* key, char* out, size_t cap) override {
    return inner_.readString(key, out, cap);
  }
  bool has(const char* key) override { return inner_.has(key); }
  size_t readBlob(const char* key, void* out, size_t cap) override {
    return inner_.readBlob(key, out, cap);
  }
  size_t write(const char* key, const char* value) override {
    if (!spend()) return 0;
    if (refuseWhole_ && std::strcmp(key, "cfg") == 0 &&
        std::strstr(value, "\"expressions\":") != nullptr) {
      refuseWhole_ = false;
      return 0;
    }
    return inner_.write(key, value);
  }
  size_t writeBlob(const char* key, const void* data, size_t len) override {
    return spend() ? inner_.writeBlob(key, data, len) : 0;
  }
  bool erase(const char* key) override { return spend() && inner_.erase(key); }
  bool clear() override { return inner_.clear(); }
  size_t spent = 0;

 private:
  bool spend() {
    if (spent == budget_) return false;
    ++spent;
    return true;
  }
  ConfigStore& inner_;
  size_t budget_;
  bool refuseWhole_;
};

// A reset runs IDF's boot cleanup before the load.
void reboot(InMemoryConfigStore&) {}
void reboot(PagedNvs& nvs) { nvs.boot(); }

// Saves `m` over a copy of `start`, cut short after each write in turn, and
// passes each cut store with what it loads as to `check`, until the save
// finishes; the finished store must load as `m`. A save that has not
// finished within four times the writes it takes uncut fails.
template <class Store, class Check>
bool cutAtEveryWrite(const Store& start, const Model& m, bool refuseWhole, Check check) {
  size_t cap = 0;
  {
    Store store = start;
    CrashAfter counting(store, SIZE_MAX, refuseWhole);
    save(counting, m);
    cap = 4 * counting.spent + 4;
  }
  for (size_t budget = 0; budget <= cap; ++budget) {
    Store store = start;
    CrashAfter crashing(store, budget, refuseWhole);
    const bool finished = save(crashing, m) == SaveResult::Ok && crashing.spent < budget;
    reboot(store);
    Model back;
    load(store, back);
    if (finished) return allSections(back, m);
    if (!check(store, back)) return false;
  }
  return false;
}

// From a store holding `a`, every cut-short save of `b`, and from each store
// it leaves, every cut-short save of `c`: each load holds each section as the
// store before that save loaded it, or as saved.
template <class Store>
bool survivesCutShortStarts(const Store& holdingA, const Model& a, const Model& b,
                            bool refuseB, const Model& c, bool refuseC, size_t& checks) {
  return cutAtEveryWrite(holdingA, b, refuseB, [&](const Store& cut, const Model& mid) {
    ++checks;
    if (!eachSectionOneOf(mid, a, b)) return false;
    return cutAtEveryWrite(cut, c, refuseC, [&](const Store&, const Model& back) {
      ++checks;
      return eachSectionOneOf(back, mid, c);
    });
  });
}

// Distinct in every section, so a load shows which save each came from.
std::vector<Model> distinctShapes() {
  Model small;  // whole
  small.lamp.name = "small";
  small.lamp.brightness = 11;
  small.expressions.expressions = {tagged("pulse", 1, 1)};
  small.base.segments = {{"Base", 3, {Color(1, 1, 1, 1)}}};
  Model wide = small;  // whole, the largest
  wide.lamp.name = "wide";
  wide.lamp.brightness = 22;
  wide.base.segments = {{"Base", 3, {Color(2, 2, 2, 2)}}};
  wide.expressions.expressions = {fullExpression("pulse", 1), fullExpression("shifty", 2)};
  Model big = worstCaseModel('z');  // split
  big.lamp.name = "big";
  big.lamp.brightness = 33;
  big.expressions.expressions.resize(3);
  Model quoted = worstCaseModel('"');  // split, fully escaped
  quoted.lamp.brightness = 44;
  quoted.base.segments[0].colors[0] = Color(4, 4, 4, 4);
  quoted.expressions.expressions.resize(2);
  quoted.expressions.expressions[0].intervalMin = 4;
  // Each as a load gives it back, so a comparison sees only storage.
  std::vector<Model> shapes;
  for (const Model& m : {small, wide, big, quoted}) {
    InMemoryConfigStore canonical;
    save(canonical, m);
    shapes.emplace_back();
    load(canonical, shapes.back());
  }
  return shapes;
}

// MAJOR path: every shape to every shape, staged or not, cut short at every
// write, and from every store that leaves, the next save cut short too.
// Every shape to every shape and on to every shape, each whole save also
// with its string refused (the staged path), cut short at every write, and
// the second save cut short from every store the first leaves.
void test_every_cut_short_store_survives_the_next_save() {
  const auto shapes = distinctShapes();
  // A whole target, also refused once; a split one has no string to refuse.
  struct Target {
    size_t shape;
    bool refuse;
  };
  std::vector<Target> targets;
  for (size_t i = 0; i < shapes.size(); ++i) {
    targets.push_back({i, false});
    if (i < 2) targets.push_back({i, true});
  }
  size_t checks = 0;
  for (size_t ai = 0; ai < shapes.size(); ++ai) {
    InMemoryConfigStore holdingA;
    TEST_ASSERT_TRUE(save(holdingA, shapes[ai]) == SaveResult::Ok);
    for (const Target& b : targets) {
      for (const Target& c : targets) {
        const bool ok = survivesCutShortStarts(holdingA, shapes[ai], shapes[b.shape], b.refuse,
                                               shapes[c.shape], c.refuse, checks);
        char what[96];
        std::snprintf(what, sizeof(what), "%s -> %s%s -> %s%s", shapes[ai].lamp.name.c_str(),
                      shapes[b.shape].lamp.name.c_str(), b.refuse ? " (staged)" : "",
                      shapes[c.shape].lamp.name.c_str(), c.refuse ? " (staged)" : "");
        TEST_ASSERT_TRUE_MESSAGE(ok, what);
      }
    }
  }
  printf("[crash] %zu cut-short loads checked\n", checks);
}

// Three saves cut short in a row: a split save with "cfg" erased (its lamp
// section in "cfgl"), then a whole save staged as a rest blob with "cfg"
// erased. The staged whole now holds the lamp section and "cfgl" is stale, so
// the next split save must stage its own rather than keep it.
void test_a_stale_lamp_blob_under_a_staged_whole_is_replaced() {
  const auto shapes = distinctShapes();
  const Model& y = shapes[2];  // split
  const Model& w = shapes[0];  // whole
  const Model& z = shapes[3];  // split
  InMemoryConfigStore store;
  TEST_ASSERT_TRUE(save(store, y) == SaveResult::Ok);
  static char lampJson[256];
  JsonWriter lw(lampJson, sizeof(lampJson));
  config_codec::writeLampOnlyJson(lw, y.lamp);
  lw.finish();
  store.writeBlob("cfgl", lampJson, lw.length());
  store.erase("cfg");
  {
    // A refused whole string, the rest blob erased and written, the older one
    // erased, then "cfg": five writes.
    CrashAfter crashing(store, 5, true);
    TEST_ASSERT_TRUE(save(crashing, w) == SaveResult::WriteFailed);
  }
  TEST_ASSERT_TRUE(store.has("cfgl") && !store.has("cfg"));
  Model mid;
  load(store, mid);
  TEST_ASSERT_TRUE(allSections(mid, w));
  TEST_ASSERT_TRUE(cutAtEveryWrite(store, z, false, [&](const InMemoryConfigStore&,
                                                        const Model& back) {
    return eachSectionOneOf(back, w, z);
  }));
}

// A store cut between writing the new rest blob and erasing the old one has
// both: either is a whole rest, and the load takes the same one the next save
// treats as live.
void test_a_store_with_both_rest_keys_loads_and_saves_on() {
  const auto shapes = distinctShapes();
  const Model& before = shapes[2];
  const Model& after = shapes[3];
  bool sawBoth = false;
  for (bool beforeInB : {true, false}) {
    InMemoryConfigStore start;
    TEST_ASSERT_TRUE(save(start, before) == SaveResult::Ok);
    if (!beforeInB) {
      TEST_ASSERT_TRUE(save(start, after) == SaveResult::Ok);  // live in cfgc
    }
    TEST_ASSERT_TRUE(start.has("cfgb") != start.has("cfgc"));
    const Model& old = beforeInB ? before : after;
    const Model& next = beforeInB ? after : before;
    TEST_ASSERT_TRUE(cutAtEveryWrite(start, next, false, [&](InMemoryConfigStore& cut,
                                                            const Model& back) {
      if (!(cut.has("cfgb") && cut.has("cfgc"))) return eachSectionOneOf(back, old, next);
      sawBoth = true;
      if (!eachSectionOneOf(back, old, next)) return false;
      // The next save from here lands whole.
      InMemoryConfigStore copy = cut;
      Model again;
      return save(copy, next) == SaveResult::Ok && (load(copy, again), allSections(again, next));
    }));
  }
  TEST_ASSERT_TRUE(sawBoth);
}

// A staged whole save erases a "cfgl" a save cut short left before it places
// the whole string, so the string's room is counted without it.
void test_a_staged_whole_save_places_cfg_without_cfgl() {
  struct Spy : CrashAfter {
    using CrashAfter::CrashAfter;
    ConfigStore* inner = nullptr;
    bool cfglBeside = false;
    size_t write(const char* key, const char* value) override {
      const bool beside = inner->has("cfgl");
      const size_t written = CrashAfter::write(key, value);
      if (written > 0 && std::strcmp(key, "cfg") == 0 &&
          std::strstr(value, "\"expressions\":") != nullptr) {
        cfglBeside |= beside;
      }
      return written;
    }
  };
  const auto shapes = distinctShapes();
  InMemoryConfigStore store;
  save(store, shapes[2]);
  store.writeBlob("cfgl", "{\"lamp\":{}}", 11);
  store.erase("cfg");
  Spy spy(store, SIZE_MAX, true);
  spy.inner = &store;
  TEST_ASSERT_TRUE(save(spy, shapes[0]) == SaveResult::Ok);
  TEST_ASSERT_FALSE(spy.cfglBeside);
}

struct Churn {
  size_t wholeStaged = 0;  // whole saves whose string NVS refused at first
  size_t shapeChanges = 0;
  size_t crashChecks = 0;
  size_t followUps = 0;  // shape changes also cut short from every cut store
};

bool isWhole(PagedNvs& nvs) {
  return nvs.read("cfg", "").find("\"expressions\":") != std::string::npos;
}

// Saves through every shape the store takes (whole, the largest whole, split
// at every escaped worst case, the standard variant) in a seeded order, with
// the system keys and dispositions rewritten between saves the way bonding,
// boots and dispositions churn them, and `pad` one-entry keys beside them.
// Every write but a refused string must land, and every save must load back
// as saved. With `crashes`, a save that took a staged path or changed the
// store's shape is also cut short after each of its writes, and the first
// `followUpLimit` of them are cut short again from every store that leaves,
// by the next shape's save.
bool churnFits(size_t pad, uint32_t seed, size_t rounds, bool crashes, Churn& churn,
               size_t followUpLimit = 0) {
  PagedNvs nvs;
  const auto system = systemKeys();
  for (const auto& k : system) {
    if (!nvs.putSystem(k.key, k.len)) return false;
  }
  for (size_t i = 0; i < pad; ++i) {
    if (!nvs.putSystem("pad/" + std::to_string(i), 0)) return false;
  }
  if (nvs.write("lampType", "standard") == 0) return false;

  Model standard = worstCaseModel();
  standard.expressions.expressions.clear();
  for (const char* type : {"glitchy", "pulse", "breathing", "shifty", "spotty", "flicker"}) {
    for (uint8_t target = 1; target <= 3; ++target) {
      standard.expressions.expressions.push_back(fullExpression(type, target));
    }
  }
  // Each shape as a load gives it back, so a comparison sees only storage.
  std::vector<Model> shapes;
  for (const Model& m : {Model{}, largestWholeModel(), worstCaseModel('z'), worstCaseModel('"'),
                         worstCaseModel('\\'), standard}) {
    InMemoryConfigStore canonical;
    save(canonical, m);
    shapes.emplace_back();
    load(canonical, shapes.back());
  }
  uint8_t disp[DispositionStore::kMaxBlobLen];
  Model old;
  load(nvs, old);
  for (size_t round = 0; round < rounds; ++round) {
    Model m = shapes[nextRandom(seed) % shapes.size()];
    m.lamp.brightness = static_cast<uint8_t>(round);
    if (!m.expressions.expressions.empty()) {
      m.expressions.expressions[round % m.expressions.expressions.size()].intervalMin =
          static_cast<uint32_t>(round);
    }
    const PagedNvs before = nvs;
    const size_t refused = nvs.failedWrites;
    const bool wasWhole = isWhole(nvs);
    if (save(nvs, m) != SaveResult::Ok) return false;
    const bool staged = nvs.failedWrites != refused;
    const bool changed = wasWhole != isWhole(nvs);
    churn.wholeStaged += staged;
    churn.shapeChanges += changed;
    if (crashes && (staged || changed)) {
      Model next = shapes[(round + 1) % shapes.size()];
      next.lamp.brightness = static_cast<uint8_t>(round + 128);
      const bool follow = churn.followUps < followUpLimit;
      churn.followUps += follow;
      const bool ok = cutAtEveryWrite(before, m, false, [&](const PagedNvs& cut, const Model& mid) {
        ++churn.crashChecks;
        if (!eachSectionOneOf(mid, old, m)) return false;
        if (!follow) return true;
        return cutAtEveryWrite(cut, next, false, [&](const PagedNvs&, const Model& back) {
          ++churn.crashChecks;
          return eachSectionOneOf(back, mid, next);
        });
      });
      if (!ok) return false;
    }
    Model back;
    load(nvs, back);
    if (!allSections(back, m)) return false;
    old = back;
    const auto& k = system[nextRandom(seed) % system.size()];
    if (!nvs.putSystem(k.key, k.len, static_cast<uint32_t>(round))) return false;
    std::memset(disp, static_cast<int>(round), sizeof(disp));
    if (nvs.writeBlob("disp", disp, nextRandom(seed) % sizeof(disp) + 1) == 0) return false;
  }
  return true;
}

// The page model refuses a string no page has room for, though the entries
// free across the pages would hold it, and spreads a blob of the same size.
void test_page_model_confines_a_string_to_one_page() {
  PagedNvs nvs;
  for (size_t i = 0; i < kDataPages * kPageEntries; ++i) {
    TEST_ASSERT_TRUE(nvs.putSystem("p/" + std::to_string(i), 0));
  }
  for (size_t page = 0; page < kDataPages; ++page) {
    for (size_t i = 0; i < 20; ++i) {
      nvs.eraseSystem("p/" + std::to_string(page * kPageEntries + i));
    }
  }
  TEST_ASSERT_EQUAL(kDataPages * (kPageEntries - 20), nvs.liveEntries());
  const std::string thirty(29 * 32 - 1, 's');
  TEST_ASSERT_EQUAL(0, nvs.write("s", thirty.c_str()));
  TEST_ASSERT_EQUAL(thirty.size(), nvs.writeBlob("b", thirty.data(), thirty.size()));
  std::string back(thirty.size(), '\0');
  TEST_ASSERT_EQUAL(thirty.size(), nvs.readBlob("b", &back[0], back.size()));
  TEST_ASSERT_TRUE(back == thirty);
}

// IDF's cleanup of a failed blob rewrite erases chunk 0 on the page it wrote
// to, which is the old version's chunk there: the index remains until a read
// or a boot finds the chunk missing and erases the blob.
void test_page_model_failed_rewrite_takes_the_old_copy() {
  PagedNvs nvs;
  TEST_ASSERT_EQUAL(3, nvs.writeBlob("k", "old", 3));
  nvs.failBlobKey = "k";
  TEST_ASSERT_EQUAL(0, nvs.writeBlob("k", "new", 3));
  TEST_ASSERT_TRUE(nvs.has("k"));
  PagedNvs rebooted = nvs;
  // The read finds the chunk missing and erases the blob, as a boot does.
  char back[3];
  TEST_ASSERT_EQUAL(0, nvs.readBlob("k", back, sizeof(back)));
  TEST_ASSERT_FALSE(nvs.has("k"));
  rebooted.boot();
  TEST_ASSERT_FALSE(rebooted.has("k"));
}

// A rest blob rewrite that fails that way leaves the live rest readable: the
// new one goes to the other key.
void test_failed_rest_rewrite_keeps_the_live_copy() {
  PagedNvs nvs;
  Model m = worstCaseModel();
  TEST_ASSERT_TRUE(save(nvs, m) == SaveResult::Ok);
  load(nvs, m);
  Model edited = m;
  edited.base.segments[0].colors[0] = Color(7, 7, 7, 7);
  nvs.failBlobKey = nvs.has("cfgb") ? "cfgc" : "cfgb";
  TEST_ASSERT_TRUE(save(nvs, edited) == SaveResult::WriteFailed);
  Model back;
  load(nvs, back);
  TEST_ASSERT_TRUE(allSections(back, m));
  TEST_ASSERT_TRUE(save(nvs, edited) == SaveResult::Ok);
  load(nvs, back);
  TEST_ASSERT_TRUE(allSections(back, edited));
}

// A blob write whose bookkeeping allocation fails leaves an orphan chunk in
// the version it was writing. The saves after it all land in the same
// session, whichever key it was (a record, the knockout blob, the rest), and
// the boot after them loads the latest: each such key's next write goes
// through a throwaway version first. Without that, the next write doubles
// the orphan's position and the boot drops the blob.
void test_saves_after_an_out_of_memory_write_land_and_survive_the_boot() {
  auto edit = [](Model& m, int i) {
    m.expressions.expressions[3].intervalMin = 100 + i;
    m.base.knockoutPixels[0] = static_cast<uint8_t>(i);
    m.base.segments[0].colors[0] = Color(i, i, i, i);
  };
  for (const char* key : {"x3", "ko", "rest"}) {
    for (int later : {1, 2, 5}) {
      for (bool pastOrphans : {true, false}) {
        if (!pastOrphans && later != 1) continue;
        PagedNvs nvs;
        nvs.pastOrphans = pastOrphans;
        Model latest = worstCaseModel();
        TEST_ASSERT_TRUE(save(nvs, latest) == SaveResult::Ok);
        load(nvs, latest);
        nvs.outOfMemoryKey =
            std::strcmp(key, "rest") != 0 ? key : (nvs.has("cfgb") ? "cfgc" : "cfgb");
        edit(latest, 1);
        TEST_ASSERT_TRUE(save(nvs, latest) == SaveResult::WriteFailed);
        for (int i = 0; i < later; ++i) {
          edit(latest, 2 + i);
          TEST_ASSERT_TRUE_MESSAGE(save(nvs, latest) == SaveResult::Ok, key);
        }
        nvs.boot();
        Model back;
        load(nvs, back);
        if (pastOrphans) {
          TEST_ASSERT_TRUE_MESSAGE(allSections(back, latest), key);
        } else {
          TEST_ASSERT_FALSE_MESSAGE(allSections(back, latest), key);
        }
      }
    }
  }
}

// A key's write that ran out of heap is held until boot: every later write
// of it is a throwaway version, then the value.
void test_a_key_is_written_past_its_orphan_until_boot() {
  OrphanKeys orphans;
  std::vector<size_t> lengths;
  BlobWrite next = BlobWrite::OutOfMemory;
  auto write = [&](const char*, const void*, size_t len) {
    lengths.push_back(len);
    const BlobWrite result = next;
    next = BlobWrite::Ok;
    return result;
  };
  const char value[3] = {1, 2, 3};
  TEST_ASSERT_FALSE(writeBlobPastOrphan(orphans, "x3", value, 3, write));
  TEST_ASSERT_TRUE(orphans.holds("x3"));
  TEST_ASSERT_TRUE(writeBlobPastOrphan(orphans, "x3", value, 3, write));
  TEST_ASSERT_TRUE(writeBlobPastOrphan(orphans, "x3", value, 3, write));
  TEST_ASSERT_TRUE(writeBlobPastOrphan(orphans, "x2", value, 3, write));
  const std::vector<size_t> expected = {3, 0, 3, 0, 3, 3};
  TEST_ASSERT_TRUE(lengths == expected);
}

// The orphan list names keys exactly, and past kMax keys holds every key.
void test_the_orphan_list_holds_its_keys() {
  OrphanKeys keys;
  keys.add("cfgc");
  TEST_ASSERT_TRUE(keys.holds("cfgc"));
  TEST_ASSERT_FALSE(keys.holds("cfgb"));
  for (size_t i = 0; i <= OrphanKeys::kMax; ++i) keys.add(("x" + std::to_string(i)).c_str());
  TEST_ASSERT_TRUE(keys.holds("anything"));
}

// A split store whose rest blob is gone loads defaults for it, and says so,
// so the first-boot save does not write those defaults over the store.
void test_a_split_store_missing_its_rest_is_reported() {
  InMemoryConfigStore store;
  save(store, worstCaseModel());
  store.erase("cfgb");
  store.erase("cfgc");
  TEST_ASSERT_TRUE(loadsUnreadable(store));
  InMemoryConfigStore fresh;
  TEST_ASSERT_FALSE(loadsUnreadable(fresh));
}

// A store an older release wrote: a whole cfg string of up to 4,000 bytes
// (the most an NVS string holds), its dispositions JSON migrated to the blob
// at load, and the system keys. `expressions` full, colour-only or
// one-colour expressions beside a plain or a fully escaped rest.
Model legacyModel(size_t expressions, int kind, bool escapedRest) {
  Model m = escapedRest ? worstCaseModel('"') : Model{};
  m.expressions.expressions.clear();
  for (size_t i = 0; i < expressions; ++i) {
    ExpressionConfig e = fullExpression("pulse", 1 + i % 3);
    if (kind == 1) e.parameters = ExpressionParams{};
    if (kind == 2) e.colors.resize(1);
    m.expressions.expressions.push_back(e);
  }
  return m;
}

// The first save after the update, of `m` as it loads (split) or trimmed to
// one expression (whole), beside `pad` spare one-entry keys. -1 when the
// legacy string would pass 4,000 bytes.
int legacyFirstSave(const Model& m, bool trimToWhole, size_t pad) {
  PagedNvs nvs;
  for (const auto& k : systemKeys()) nvs.putSystem(k.key, k.len);
  for (size_t i = 0; i < pad; ++i) nvs.putSystem("pad/" + std::to_string(i), 0);
  nvs.write("lampType", "standard");
  static char legacy[8192];
  JsonWriter w(legacy, sizeof(legacy));
  config_codec::writeStoredJson(w, &m.lamp, m.base, m.shade, m.homeMode, &m.expressions);
  w.finish();
  if (!w.ok() || w.length() > 4000) return -1;
  const std::string legacyDispositions(2200, 'd');
  if (nvs.write("cfg", legacy) == 0 ||
      nvs.write("dispositions", legacyDispositions.c_str()) == 0) {
    return 0;
  }
  uint8_t disp[DispositionStore::kMaxBlobLen] = {1};
  if (nvs.writeBlob("disp", disp, sizeof(disp)) == 0) return 0;
  nvs.erase("dispositions");
  Model loaded;
  load(nvs, loaded);
  if (trimToWhole) loaded.expressions.expressions.resize(1);
  loaded.lamp.brightness ^= 1;
  if (save(nvs, loaded) != SaveResult::Ok) return 0;
  Model back;
  load(nvs, back);
  return allSections(back, loaded) ? 1 : 0;
}

// Every legacy shape up to 4,000 bytes saves into both shapes after the
// update with at least kMargin entries to spare: the split keys copy only
// what the legacy string held.
void test_the_first_save_after_an_update_fits() {
  size_t tightest = SIZE_MAX;
  for (bool escaped : {false, true}) {
    for (int kind : {0, 1, 2}) {
      for (size_t n = 1; n <= kMaxExpressions; ++n) {
        const Model m = legacyModel(n, kind, escaped);
        for (bool whole : {false, true}) {
          const int first = legacyFirstSave(m, whole, kMargin);
          if (first < 0) continue;
          TEST_ASSERT_EQUAL(1, first);
          size_t lo = kMargin, hi = 400;
          while (lo + 1 < hi) {
            const size_t mid = (lo + hi) / 2;
            (legacyFirstSave(m, whole, mid) == 1 ? lo : hi) = mid;
          }
          tightest = std::min(tightest, lo);
        }
      }
    }
  }
  printf("[budget] first save after an update: %zu entries spare at the tightest\n", tightest);
}

void test_every_store_shape_survives_churn_with_margin() {
  Churn churn;
  // Seed 107 leaves the least room of the first 150; seed
  // 69 switches to split where no page has room for the lamp string beside
  // the whole one.
  for (uint32_t seed : {1u, 7u, 42u, 69u, 107u}) {
    TEST_ASSERT_TRUE(churnFits(kMargin, seed, 400, true, churn, seed == 1 ? 12 : 0));
  }
  printf("[budget] churn: %zu staged whole saves, %zu shape changes, %zu follow-ups, "
         "%zu crash points\n",
         churn.wholeStaged, churn.shapeChanges, churn.followUps, churn.crashChecks);
  TEST_ASSERT_TRUE(churn.wholeStaged > 0);
  TEST_ASSERT_TRUE(churn.shapeChanges > 0);
  size_t pad = kMargin;
  while (churnFits(pad + 1, 1, 400, false, churn)) ++pad;
  printf("[budget] churn holds with %zu spare entries\n", pad);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_record_round_trip);
  RUN_TEST(test_worst_record_fits_the_bound);
  RUN_TEST(test_truncated_record_rejected);
  RUN_TEST(test_record_caps_parameters);
  RUN_TEST(test_save_load_round_trip);
  RUN_TEST(test_legacy_single_json_loads_and_small_config_stays_whole);
  RUN_TEST(test_split_then_whole_erases_split_keys);
  RUN_TEST(test_config_saved_by_older_firmware_wins_over_stale_records);
  RUN_TEST(test_too_large_writes_nothing);
  RUN_TEST(test_no_record_is_orphaned_by_a_reset);
  RUN_TEST(test_delete_all_survives_a_reset_before_the_record_erase);
  RUN_TEST(test_split_save_sweeps_records_past_its_count);
  RUN_TEST(test_saves_sweep_record_slots_past_the_cap);
  RUN_TEST(test_over_long_fields_cannot_fail_every_save);
  RUN_TEST(test_removed_expressions_erase_stale_records);
  RUN_TEST(test_failed_write_reports_and_keeps_the_old_config);
  RUN_TEST(test_page_model_confines_a_string_to_one_page);
  RUN_TEST(test_page_model_failed_rewrite_takes_the_old_copy);
  RUN_TEST(test_failed_rest_rewrite_keeps_the_live_copy);
  RUN_TEST(test_saves_after_an_out_of_memory_write_land_and_survive_the_boot);
  RUN_TEST(test_a_split_store_missing_its_rest_is_reported);
  RUN_TEST(test_the_orphan_list_holds_its_keys);
  RUN_TEST(test_a_key_is_written_past_its_orphan_until_boot);
  RUN_TEST(test_every_cut_short_store_survives_the_next_save);
  RUN_TEST(test_a_stale_lamp_blob_under_a_staged_whole_is_replaced);
  RUN_TEST(test_a_store_with_both_rest_keys_loads_and_saves_on);
  RUN_TEST(test_a_staged_whole_save_places_cfg_without_cfgl);
  RUN_TEST(test_save_buffer_holds_the_worst_rest);
  RUN_TEST(test_widest_rotating_rest_fits_under_the_worst_rest);
  RUN_TEST(test_web_document_round_trip_keeps_rotation);
  RUN_TEST(test_whole_cfg_string_is_always_placed);
  RUN_TEST(test_lamp_cfg_string_is_always_placed);
  RUN_TEST(test_small_blobs_are_always_placed);
  RUN_TEST(test_split_peaks_fit_the_entries);
  RUN_TEST(test_the_first_save_after_an_update_fits);
  RUN_TEST(test_every_store_shape_survives_churn_with_margin);
  RUN_TEST(test_crash_mid_save_removing_an_entry_keeps_the_rest);
  RUN_TEST(test_crash_mid_save_adding_and_editing_keeps_the_rest);
  RUN_TEST(test_crash_mid_save_crossing_the_split_keeps_the_rest);
  RUN_TEST(test_crash_mid_migration_keeps_the_legacy_config);
  RUN_TEST(test_load_of_a_whole_store_drops_stale_blobs);
  RUN_TEST(test_split_keeps_the_lamp_section_alone_in_cfg);
  RUN_TEST(test_the_whole_cap_is_exact);
  RUN_TEST(test_an_unreadable_rest_blob_is_reported);
  RUN_TEST(test_an_unreadable_lamp_blob_is_reported);
  RUN_TEST(test_unreadable_record_drops_only_itself);
  RUN_TEST(test_cfg_json_over_the_buffer_fails_cleanly);
  RUN_TEST(test_unregistered_type_record_loads);
  return UNITY_END();
}
