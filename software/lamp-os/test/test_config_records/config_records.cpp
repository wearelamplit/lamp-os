// Native tests for the config storage: a compact record per expression and
// the rest of the config as one settings blob ("st0" / "st1" by turns). Pins
// the round trip, the migration from every form releases up to 1.3.3 wrote
// (cut short at every write), a failed or mismatched write, and the NVS
// budget at the caps (docs/dev/embedded-heap.md shows the arithmetic).

#include <unity.h>

#include <ArduinoJson.h>

#include <algorithm>
#include <string>
#include <vector>

#include "config/config_codec.hpp"
#include "config/disposition_store.hpp"
#include "core/boot_history.hpp"
#include "core/crash_record.hpp"
#include "config_model.hpp"
#include "legacy_store.hpp"
#include "paged_nvs.hpp"

#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;
using config_codec::SaveResult;
using config_codec::StoreForm;

void setUp(void) {}
void tearDown(void) {}

namespace {

uint8_t s_buf[config_codec::kMaxSettingsBlobLen];

SaveResult save(ConfigStore& store, const Model& m) {
  return config_codec::saveConfig(store, m.lamp, m.base, m.shade, m.expressions, m.homeMode,
                                  s_buf, sizeof(s_buf));
}

// The load Config's constructor runs.
config_codec::LoadedConfig load(ConfigStore& store, Model& m) {
  return config_codec::loadConfig(store, m.lamp, m.base, m.shade, m.expressions, m.homeMode,
                                  s_buf, sizeof(s_buf));
}

// The save a release up to 1.3.3 ran.
SaveResult legacySave(ConfigStore& store, const Model& m) {
  static char buf[legacy133::kMaxStoredCfgJsonLen + 1];
  return legacy133::saveConfig(store, m.lamp, m.base, m.shade, m.expressions, m.homeMode, buf,
                               sizeof(buf));
}

std::string legacyJson(const Model& m) {
  static char buf[8192];
  JsonWriter w(buf, sizeof(buf));
  legacy133::writeStoredJson(w, &m.lamp, m.base, m.shade, m.homeMode, &m.expressions);
  w.finish();
  TEST_ASSERT_TRUE(w.ok());
  return std::string(buf, w.length());
}

// Adds full expressions until the whole config cannot fit one cfg JSON, so a
// legacy save splits into records.
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

const char* const kLegacyKeys[] = {"cfg", "cfgb", "cfgc", "cfgl", "ko"};

bool anyLegacyKey(ConfigStore& store) {
  for (const char* key : kLegacyKeys) {
    if (store.has(key)) return true;
  }
  return false;
}

bool sameExpression(const ExpressionConfig& a, const ExpressionConfig& b) {
  return a.type == b.type && a.target == b.target && a.enabled == b.enabled &&
         a.intervalMin == b.intervalMin && a.intervalMax == b.intervalMax &&
         a.colors == b.colors && a.parameters == b.parameters;
}

// The settings blob's part of two models, the expressions aside.
bool sameSettings(const Model& a, const Model& b) {
  Model x = a;
  Model y = b;
  x.expressions = {};
  y.expressions = {};
  return firstDifference(x, y).empty();
}

// Every loaded expression is one `a` or `b` holds: a save cut short rewrites
// some records and not others.
bool expressionsFrom(const Model& back, const Model& a, const Model& b) {
  for (const auto& e : back.expressions.expressions) {
    bool found = false;
    for (const Model* m : {&a, &b}) {
      for (const auto& o : m->expressions.expressions) found |= sameExpression(o, e);
    }
    if (!found) return false;
  }
  return true;
}

// A save cut short leaves the settings as before or as saved, and each
// expression as one of them held it.
bool eachSectionOneOf(const Model& back, const Model& a, const Model& b) {
  return (sameSettings(back, a) || sameSettings(back, b)) && expressionsFrom(back, a, b);
}

bool allSections(const Model& back, const Model& m) { return firstDifference(back, m).empty(); }

// `m` as a load of its own save gives it back, so a comparison sees only
// storage.
Model canonical(const Model& m) {
  InMemoryConfigStore store;
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  Model back;
  load(store, back);
  return back;
}

ExpressionConfig tagged(const char* type, uint8_t target, uint32_t size) {
  ExpressionConfig e;
  e.type = type;
  e.target = target;
  e.colors = {Color(1, 2, 3, 4)};
  e.setParameter("size", size);
  return e;
}

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

// A save writes the records, the count and one settings blob, and nothing a
// release up to 1.3.3 wrote.
void test_save_load_round_trip() {
  for (char fill : {'z', '"', '\\'}) {
    InMemoryConfigStore store;
    const Model m = canonical(worstCaseModel(fill));
    TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
    Model back;
    TEST_ASSERT_TRUE(load(store, back).form == StoreForm::Binary);
    TEST_ASSERT_EQUAL_STRING("", firstDifference(m, back).c_str());
    TEST_ASSERT_TRUE(store.has("st0") && !store.has("st1") && store.has("xn") && store.has("x20"));
    TEST_ASSERT_FALSE(anyLegacyKey(store));
  }
}

// A lamp updating from the single-JSON form keeps its expressions and
// knockout, and its first save moves it to the binary store.
void test_a_legacy_whole_store_loads_and_migrates() {
  InMemoryConfigStore store;
  store.write("cfg",
              "{\"lamp\":{\"name\":\"jacko\"},\"base\":{\"segments\":[{\"name\":\"Base\","
              "\"px\":3,\"colors\":[\"#11223300\"]}],\"knockout\":[10,20,30]},"
              "\"expressions\":[{\"type\":\"pulse\",\"enabled\":true,\"target\":1,"
              "\"size\":4,\"colors\":[\"#ff000000\"]}]}");
  Model m;
  TEST_ASSERT_TRUE(load(store, m).form == StoreForm::Whole);
  TEST_ASSERT_EQUAL(1, m.expressions.expressions.size());
  TEST_ASSERT_EQUAL_UINT8(20, m.base.knockoutPixels[1]);
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  TEST_ASSERT_FALSE(anyLegacyKey(store));
  Model back;
  TEST_ASSERT_TRUE(load(store, back).form == StoreForm::Binary);
  TEST_ASSERT_EQUAL_STRING("", firstDifference(m, back).c_str());
  TEST_ASSERT_EQUAL_UINT32(4, back.expressions.expressions[0].getParameter("size", 0));
  TEST_ASSERT_EQUAL_UINT8(30, back.base.knockoutPixels[2]);
}

// A lamp flashed back to 1.3.3 finds no "cfg" and saves its own, whole or
// split, beside the settings blob; flashed forward again, the settings blob
// loads, with the records the older release left (none after a whole save),
// and the keys it wrote go.
void test_a_store_saved_after_a_downgrade_loads_the_settings_blob() {
  for (bool olderSplit : {false, true}) {
    Model mine = canonical(worstCaseModel('z'));
    mine.lamp.name = "mine";
    mine = canonical(mine);
    InMemoryConfigStore store;
    TEST_ASSERT_TRUE(save(store, mine) == SaveResult::Ok);
    Model older;
    older.lamp.name = "older";
    older.expressions.expressions = {tagged("spotty", 2, 5)};
    if (olderSplit) padToSplit(older);
    TEST_ASSERT_TRUE(legacySave(store, older) == SaveResult::Ok);
    Model back;
    TEST_ASSERT_TRUE(load(store, back).form == StoreForm::Binary);
    TEST_ASSERT_TRUE(sameSettings(back, mine));
    TEST_ASSERT_EQUAL(olderSplit ? older.expressions.expressions.size() : 0,
                      back.expressions.expressions.size());
    TEST_ASSERT_FALSE(anyLegacyKey(store));
  }
}

// A buffer that cannot hold the settings blob is refused before any write.
void test_too_large_writes_nothing() {
  InMemoryConfigStore store;
  Model m;
  padToSplit(m);
  uint8_t small[config_codec::kMaxSettingsBlobLen - 1];
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

// An unchanged save writes no new settings blob.
void test_an_unchanged_save_rewrites_no_blob() {
  InMemoryConfigStore store;
  const Model m = canonical(worstCaseModel('z'));
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  TEST_ASSERT_TRUE(store.has("st0"));
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  TEST_ASSERT_TRUE(store.has("st0") && !store.has("st1"));
  Model edited = m;
  edited.lamp.brightness ^= 1;
  TEST_ASSERT_TRUE(save(store, edited) == SaveResult::Ok);
  TEST_ASSERT_TRUE(store.has("st1") && !store.has("st0"));
}

namespace {

std::vector<std::pair<std::string, uint8_t>> keysOf(const ExpressionSettings& s) {
  std::vector<std::pair<std::string, uint8_t>> keys;
  for (const auto& e : s.expressions) keys.push_back({e.type, e.target});
  std::sort(keys.begin(), keys.end());
  return keys;
}

// Saves `before`, then saves `after` with the store reset partway through,
// at every possible write; each load must be a whole config: the settings as
// one of the two saved them, every entry is one `before` or `after` held
// (same type, target and contents), every entry both hold is present, and
// nothing from neither appears.
void assertCrashSafe(Model before, Model after) {
  before.lamp.name = "before";
  before.lamp.brightness = 1;
  after.lamp.name = "after";
  after.lamp.brightness = 2;
  after.homeMode.ssid = "after";
  for (size_t crashAt = 0; crashAt < 80; ++crashAt) {
    InMemoryConfigStore store;
    save(store, before);
    store.writesBeforeCrash = crashAt;
    const bool finished = save(store, after) == SaveResult::Ok;
    store.writesBeforeCrash = SIZE_MAX;
    Model back;
    load(store, back);
    TEST_ASSERT_TRUE_MESSAGE(
        (back.lamp.name == "before" && back.lamp.brightness == 1 && back.homeMode.ssid.empty()) ||
            (back.lamp.name == "after" && back.lamp.brightness == 2 &&
             back.homeMode.ssid == "after"),
        "settings lost or mixed");
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
  for (bool padded : {false, true}) {
    Model before;
    before.expressions.expressions = {tagged("pulse", 1, 1), tagged("shifty", 2, 2),
                                      tagged("spotty", 3, 3), tagged("glitchy", 1, 4)};
    if (padded) padToSplit(before);
    Model after = before;
    after.expressions.expressions.erase(after.expressions.expressions.begin());
    assertCrashSafe(before, after);
  }
}

void test_crash_mid_save_adding_and_editing_keeps_the_rest() {
  for (bool padded : {false, true}) {
    Model before;
    before.expressions.expressions = {tagged("pulse", 1, 1), tagged("shifty", 2, 2)};
    if (padded) padToSplit(before);
    Model after = before;
    after.expressions.expressions[1] = tagged("shifty", 2, 9);
    after.expressions.expressions.push_back(tagged("spotty", 3, 3));
    assertCrashSafe(before, after);
  }
}

void test_crash_mid_save_growing_and_shrinking_keeps_the_rest() {
  Model small;
  small.expressions.expressions = {tagged("pulse", 1, 1), tagged("shifty", 2, 2)};
  Model big = small;
  padToSplit(big);
  assertCrashSafe(small, big);
  assertCrashSafe(big, small);
}

// A reset during the first save after an update leaves the single-JSON form
// readable: it stays the authority until the settings blob lands.
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

// A save reset at any write, then a later save, leaves no record past its
// count: every slot past it is swept.
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
    for (int i = 1; i < 32; ++i) {
      TEST_ASSERT_FALSE(store.has(("x" + std::to_string(i)).c_str()));
    }
    TEST_ASSERT_EQUAL_STRING("1", store.read("xn", "").c_str());
  }
}

// A save cut short before its count leaves records past the old count; the
// next save sweeps them.
void test_a_save_sweeps_records_past_its_count() {
  InMemoryConfigStore store;
  Model m;
  padToSplit(m);
  padToSplit(m);  // 8 full expressions
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
// was higher) is swept.
void test_saves_sweep_record_slots_past_the_cap() {
  const uint8_t junk[3] = {1, 2, 3};
  InMemoryConfigStore store;
  store.writeBlob("x25", junk, sizeof(junk));
  Model m;
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  TEST_ASSERT_FALSE(store.has("x25"));
}

// Deleting every expression writes a count of 0 before anything is swept: a
// reset loads all of them or none, never some of the records it removed.
void test_delete_all_survives_a_reset() {
  Model before;
  padToSplit(before);
  Model none;
  for (size_t crashAt = 0; crashAt < 60; ++crashAt) {
    InMemoryConfigStore store;
    save(store, before);
    store.writesBeforeCrash = crashAt;
    const bool finished = save(store, none) == SaveResult::Ok;
    store.writesBeforeCrash = SIZE_MAX;
    Model back;
    load(store, back);
    const size_t n = back.expressions.expressions.size();
    TEST_ASSERT_TRUE(n == 0 || n == before.expressions.expressions.size());
    if (finished) {
      TEST_ASSERT_EQUAL(0, n);
      return;
    }
  }
  TEST_FAIL_MESSAGE("save never finished");
}

// A whole cfg string is the legacy loader's authority, so rest and lamp
// blobs beside it go at load.
void test_load_of_a_legacy_whole_store_drops_stale_blobs() {
  InMemoryConfigStore store;
  Model m;
  m.lamp.name = "whole";
  TEST_ASSERT_TRUE(legacySave(store, m) == SaveResult::Ok);
  store.writeBlob("cfgb", "{}", 2);
  store.writeBlob("cfgc", "{}", 2);
  store.writeBlob("cfgl", "{}", 2);
  Model back;
  load(store, back);
  TEST_ASSERT_EQUAL_STRING("whole", back.lamp.name.c_str());
  TEST_ASSERT_FALSE(store.has("cfgb") || store.has("cfgc") || store.has("cfgl"));
}

bool loadsUnreadable(ConfigStore& store) {
  Model back;
  return load(store, back).records.unreadable;
}

// A legacy rest blob that holds something unreadable is reported, so Config
// serves defaults without letting the first-boot save write them over it:
// JSON that does not parse, a blob over the buffer, or a chunk that fails its
// CRC (the first read after it; IDF erases the chunk then).
void test_an_unreadable_legacy_rest_blob_is_reported() {
  {
    InMemoryConfigStore store;
    legacySave(store, worstCaseModel());
    TEST_ASSERT_FALSE(loadsUnreadable(store));
    store.writeBlob("cfgb", "{broken", 7);
    TEST_ASSERT_TRUE(loadsUnreadable(store));
  }
  {
    InMemoryConfigStore store;
    legacySave(store, worstCaseModel());
    const std::string over(config_codec::kMaxStoredCfgJsonLen + 1, ' ');
    store.writeBlob("cfgb", over.data(), over.size());
    TEST_ASSERT_TRUE(loadsUnreadable(store));
  }
  {
    PagedNvs nvs;
    legacySave(nvs, worstCaseModel());
    nvs.corruptBlob(nvs.has("cfgb") ? "cfgb" : "cfgc");
    TEST_ASSERT_TRUE(loadsUnreadable(nvs));
  }
}

// So is a "cfgl" holding the only lamp section when a chunk fails its CRC.
void test_an_unreadable_legacy_lamp_blob_is_reported() {
  PagedNvs nvs;
  const Model m = worstCaseModel();
  legacySave(nvs, m);
  static char lampJson[512];
  JsonWriter w(lampJson, sizeof(lampJson));
  legacy133::writeLampOnlyJson(w, m.lamp);
  w.finish();
  nvs.writeBlob("cfgl", lampJson, w.length());
  nvs.erase("cfg");
  TEST_ASSERT_FALSE(loadsUnreadable(nvs));
  nvs.corruptBlob("cfgl");
  TEST_ASSERT_TRUE(loadsUnreadable(nvs));
}

// A legacy split store whose rest blob is gone loads defaults for it, and
// says so.
void test_a_legacy_split_store_missing_its_rest_is_reported() {
  InMemoryConfigStore store;
  legacySave(store, worstCaseModel());
  store.erase("cfgb");
  store.erase("cfgc");
  TEST_ASSERT_TRUE(loadsUnreadable(store));
  InMemoryConfigStore fresh;
  TEST_ASSERT_FALSE(loadsUnreadable(fresh));
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
  const auto loaded = load(store, back);
  TEST_ASSERT_TRUE(loaded.records.present);
  TEST_ASSERT_EQUAL_UINT32(1u << 1, loaded.records.droppedMask);
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

// The web GET document carries a one-segment role's rotation and its PUT
// brings it back. The PUT parses into default-constructed settings, whose
// flag is off, the only way this path runs.
void test_web_document_round_trip_keeps_rotation() {
  Model m;
  m.shade.rotation = {true, 7, RotationDir::Up};
  m.base.rotation = {true, 33, RotationDir::Down};
  m.shade.rotationSupported = m.base.rotationSupported = true;
  const std::string body = webDocument(m);
  InMemoryConfigStore store;
  TEST_ASSERT_TRUE(config_codec::saveRawJson(store, body.c_str(), s_buf, sizeof(s_buf), "") ==
                   SaveResult::Ok);
  Model back;
  load(store, back);
  TEST_ASSERT_TRUE(back.shade.rotation == m.shade.rotation);
  TEST_ASSERT_TRUE(back.base.rotation == m.base.rotation);
}

std::vector<Model> distinctShapes();

namespace {

// Logs every store call a save makes, in order, and can corrupt what one key
// is written with.
class SpyStore : public ConfigStore {
 public:
  explicit SpyStore(ConfigStore& inner) : inner_(inner) {}
  std::vector<std::string> log;
  size_t longestString = 0;
  std::string corruptKey;  // its next write lands with one byte flipped

  std::string read(const char* key, const char* d) override {
    log.push_back(std::string("read ") + key);
    return inner_.read(key, d);
  }
  size_t readString(const char* key, char* out, size_t cap) override {
    log.push_back(std::string("read ") + key);
    return inner_.readString(key, out, cap);
  }
  size_t readBlob(const char* key, void* out, size_t cap) override {
    log.push_back(std::string("read ") + key);
    return inner_.readBlob(key, out, cap);
  }
  bool has(const char* key) override { return inner_.has(key); }
  size_t write(const char* key, const char* value) override {
    log.push_back(std::string("write ") + key);
    longestString = std::max(longestString, std::strlen(value) + 1);
    return inner_.write(key, value);
  }
  size_t writeBlob(const char* key, const void* data, size_t len) override {
    log.push_back(std::string("write ") + key);
    if (corruptKey == key && len > 0) {
      corruptKey.clear();
      std::string flipped(static_cast<const char*>(data), len);
      flipped[len / 2] ^= 0x40;
      return inner_.writeBlob(key, flipped.data(), len);
    }
    return inner_.writeBlob(key, data, len);
  }
  bool erase(const char* key) override {
    log.push_back(std::string("erase ") + key);
    return inner_.erase(key);
  }
  bool clear() override { return inner_.clear(); }

  // The position of the first entry `what`, or SIZE_MAX.
  size_t first(const std::string& what) const {
    const auto it = std::find(log.begin(), log.end(), what);
    return it == log.end() ? SIZE_MAX : static_cast<size_t>(it - log.begin());
  }
  size_t last(const std::string& what) const {
    const auto it = std::find(log.rbegin(), log.rend(), what);
    return it == log.rend() ? SIZE_MAX : static_cast<size_t>(log.rend() - it - 1);
  }

 private:
  ConfigStore& inner_;
};

// The legacy stores the spy tests migrate: whole, and split at its worst.
std::vector<InMemoryConfigStore> legacyStores() {
  std::vector<InMemoryConfigStore> stores(2);
  Model whole;
  whole.lamp.name = "whole";
  whole.expressions.expressions = {tagged("pulse", 1, 1), tagged("shifty", 2, 2)};
  TEST_ASSERT_TRUE(legacySave(stores[0], whole) == SaveResult::Ok);
  TEST_ASSERT_TRUE(legacySave(stores[1], worstCaseModel('"')) == SaveResult::Ok);
  return stores;
}

}  // namespace

// On a migrating save every record is read back before the settings blob is
// written, no legacy key is erased until the blob has been read back, and the
// live blob not until its replacement has.
void test_no_legacy_key_goes_before_the_read_back() {
  for (InMemoryConfigStore& store : legacyStores()) {
    Model m;
    load(store, m);
    SpyStore spy(store);
    TEST_ASSERT_TRUE(save(spy, m) == SaveResult::Ok);
    const size_t blobWritten = spy.first("write st0");
    const size_t blobRead = spy.last("read st0");
    TEST_ASSERT_TRUE(blobWritten < blobRead && blobRead != SIZE_MAX);
    size_t recordsRead = 0;
    for (size_t i = 0; i < m.expressions.expressions.size(); ++i) {
      recordsRead = std::max(recordsRead, spy.last("read x" + std::to_string(i)));
    }
    recordsRead = std::max(recordsRead, spy.last("read xn"));
    TEST_ASSERT_TRUE(recordsRead < blobWritten);
    for (const char* key : kLegacyKeys) {
      const size_t erased = spy.first(std::string("erase ") + key);
      if (erased == SIZE_MAX) continue;
      TEST_ASSERT_TRUE_MESSAGE(erased > blobRead, key);
    }
    TEST_ASSERT_FALSE(anyLegacyKey(store));
    // The next save replaces the live blob: its erase follows the new one's
    // read-back.
    m.lamp.brightness ^= 1;
    SpyStore next(store);
    TEST_ASSERT_TRUE(save(next, m) == SaveResult::Ok);
    TEST_ASSERT_TRUE(next.first("erase st0") > next.last("read st1"));
    TEST_ASSERT_TRUE(next.last("read st1") > next.first("write st1"));
  }
}

// A settings blob or record that reads back other than written fails the
// save and keeps the legacy store, which still loads.
void test_a_mismatched_read_back_keeps_the_legacy_store() {
  for (const char* key : {"st0", "x0"}) {
    for (InMemoryConfigStore& store : legacyStores()) {
      Model before;
      const StoreForm form = load(store, before).form;
      TEST_ASSERT_TRUE(config_codec::isLegacyForm(form));
      Model m = before;
      if (std::strcmp(key, "x0") == 0) m.expressions.expressions[0].intervalMin ^= 1;
      SpyStore spy(store);
      spy.corruptKey = key;
      TEST_ASSERT_TRUE_MESSAGE(save(spy, m) == SaveResult::WriteFailed, key);
      TEST_ASSERT_TRUE(anyLegacyKey(store));
      Model back;
      TEST_ASSERT_TRUE(load(store, back).form == form);
      TEST_ASSERT_TRUE(sameSettings(back, before));
      // The retry lands.
      TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
      TEST_ASSERT_FALSE(anyLegacyKey(store));
    }
  }
  // A blob that reads back wrong in "st0" beside a live "st1" is erased, so
  // the live one still loads.
  InMemoryConfigStore store;
  const auto shapes = distinctShapes();
  TEST_ASSERT_TRUE(save(store, shapes[0]) == SaveResult::Ok);
  TEST_ASSERT_TRUE(save(store, shapes[1]) == SaveResult::Ok);
  TEST_ASSERT_TRUE(store.has("st1") && !store.has("st0"));
  SpyStore spy(store);
  spy.corruptKey = "st0";
  TEST_ASSERT_TRUE(save(spy, shapes[2]) == SaveResult::WriteFailed);
  TEST_ASSERT_FALSE(store.has("st0"));
  Model back;
  load(store, back);
  TEST_ASSERT_TRUE(sameSettings(back, shapes[1]));
}

// The only string a save writes is the count.
void test_no_save_writes_a_string_over_three_bytes() {
  for (InMemoryConfigStore& store : legacyStores()) {
    for (const Model& m : {worstCaseModel('z'), Model{}, worstCaseModel('\\')}) {
      SpyStore spy(store);
      TEST_ASSERT_TRUE(save(spy, m) == SaveResult::Ok);
      TEST_ASSERT_TRUE(spy.longestString <= 3);
    }
  }
}

// A legacy store's first boot saves once (Lamp::setup persists while the
// store is legacy or predates `named`); the next boot wants no save and its
// load writes nothing.
void test_a_legacy_boot_saves_once() {
  for (InMemoryConfigStore& store : legacyStores()) {
    Model m;
    const auto first = load(store, m);
    TEST_ASSERT_TRUE(firstBootSaveWanted(first.namedKeyPresent, config_codec::isLegacyForm(first.form)));
    TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
    SpyStore spy(store);
    Model again;
    const auto second = load(spy, again);
    TEST_ASSERT_FALSE(
        firstBootSaveWanted(second.namedKeyPresent, config_codec::isLegacyForm(second.form)));
    for (const auto& op : spy.log) TEST_ASSERT_TRUE_MESSAGE(op.rfind("read ", 0) == 0, op.c_str());
    TEST_ASSERT_TRUE(allSections(again, m));
  }
}

// NVS budget against the 20 KB partition: 5 pages of 126 entries, one held
// free for garbage collection. docs/dev/embedded-heap.md shows the arithmetic.
constexpr size_t kPageEntries = PagedNvs::kEntries;
constexpr size_t kDataPages = PagedNvs::kPages - 1;
constexpr size_t kMargin = 29;

// The keys outside the lamp namespace, as measured or bounded from each
// owner's store format.
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

// Every record at its cap, and the count.
size_t recordEntries() {
  return kMaxExpressions * blobEntries(config_codec::kMaxExpressionRecordLen) + stringEntries(2);
}

size_t settingsEntries() { return blobEntries(config_codec::kMaxSettingsLen); }

// Every key live in a binary store at the caps.
size_t binaryLive() {
  return systemEntries() + lampExtraEntries() + recordEntries() + settingsEntries();
}

// A string of `span` entries is placed when some page, compacted, has that
// many free: the page with the fewest live entries holds at most a quarter of
// the `live` entries on the four data pages. Returns the live entries that
// could still be added before that guarantee breaks.
long stringPlacementMargin(size_t live, size_t span) {
  return static_cast<long>(kDataPages * (kPageEntries - span) + kDataPages - 1) -
         static_cast<long>(live);
}

// The JSON a 1.3.3 split store holds at the caps: the rest blob and the
// lamp-only cfg string.
std::string legacyPart(const Model& m, bool lamp) {
  static char buf[legacy133::kMaxStoredCfgJsonLen + 1];
  JsonWriter w(buf, sizeof(buf));
  if (lamp) {
    legacy133::writeLampOnlyJson(w, m.lamp);
  } else {
    legacy133::writeStoredJson(w, nullptr, m.base, m.shade, m.homeMode, nullptr);
  }
  w.finish();
  TEST_ASSERT_TRUE(w.ok());
  return std::string(buf, w.length());
}

// The only strings left are the count and lampType, rewritten beside every
// key at its cap.
void test_strings_are_always_placed() {
  const size_t span = stringEntries(16);
  const size_t live = binaryLive() + span;
  const long margin = stringPlacementMargin(live, span);
  printf("[budget] strings: %zu entries, %zu live, margin %ld\n", span, live, margin);
  TEST_ASSERT_TRUE(margin >= static_cast<long>(kMargin));
}

// A blob under 400 bytes is placed like a string: IDF writes its one chunk
// whole and moves to a fresh page rather than split it while the tail holds
// less than 400 bytes. The largest is a disposition blob just under that
// (14 entries), rewritten beside every key at its cap.
void test_small_blobs_are_always_placed() {
  const size_t span = 1 + (399 + 31) / 32;
  const long margin = stringPlacementMargin(binaryLive(), span);
  printf("[budget] small blob: %zu entries, %zu live, margin %ld\n", span, binaryLive(), margin);
  TEST_ASSERT_TRUE(margin >= static_cast<long>(kMargin));
}

// Blob writes need the new copy's entries before the old copy goes. The
// binary store's peak is its largest blob rewritten: PHY calibration (1904
// bytes) or the settings blob. Migrating from the largest 1.3.3 split store
// adds the settings blob beside every legacy key, its lamp section staged in
// "cfgl" included; a 1.3.3 whole store is smaller than that.
void test_peaks_fit_the_entries() {
  const size_t steady = binaryLive() + std::max(settingsEntries(), blobEntries(1904));
  const long spare = static_cast<long>(kDataPages * kPageEntries) - static_cast<long>(steady);
  printf("[budget] binary: settings blob %zu entries, %zu live, peak %zu of %zu, spare %ld\n",
         settingsEntries(), binaryLive(), steady, kDataPages * kPageEntries, spare);
  TEST_ASSERT_TRUE(steady + kMargin <= kDataPages * kPageEntries);
  for (char fill : {'z', '"', '\\'}) {
    const Model m = worstCaseModel(fill);
    const size_t lampLen = legacyPart(m, true).size();
    const size_t legacyLive = systemEntries() + lampExtraEntries() + recordEntries() +
                              blobEntries(255) + blobEntries(legacyPart(m, false).size()) +
                              stringEntries(lampLen) + blobEntries(lampLen);
    const size_t migrating = legacyLive + settingsEntries();
    const size_t whole = systemEntries() + lampExtraEntries() +
                         stringEntries(legacy133::kMaxWholeCfgLen) + recordEntries() +
                         settingsEntries();
    printf("[budget] migrating '%c': from split %zu live, peak %zu; from whole peak %zu of %zu\n",
           fill, legacyLive, migrating, whole, kDataPages * kPageEntries);
    TEST_ASSERT_TRUE(migrating + kMargin <= kDataPages * kPageEntries);
    TEST_ASSERT_TRUE(whole + kMargin <= kDataPages * kPageEntries);
  }
}

// The largest whole config 1.3.3 saved: full expressions until the next
// would pass its whole cap.
Model largestWholeModel() {
  Model m;
  m.lamp.name = std::string(kMaxLampNameLen, 'w');
  static char buf[legacy133::kMaxWholeCfgLen + 1];
  for (size_t i = 0; i < kMaxExpressions; ++i) {
    m.expressions.expressions.push_back(fullExpression("pulse", 1 + i % 3));
    JsonWriter w(buf, sizeof(buf));
    legacy133::writeStoredJson(w, &m.lamp, m.base, m.shade, m.homeMode, &m.expressions);
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

// Fails writes and erases past `budget`, the way a reset mid-save leaves NVS.
// With `refuseWhole`, the first whole cfg string written is refused, the way
// NVS refuses one it has no page for, which drives 1.3.3's staged whole save.
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
// finishes; the finished store must load as `m`, in the binary form. A save
// that has not finished within four times the writes it takes uncut fails.
template <class Store, class Check>
bool cutAtEveryWrite(const Store& start, const Model& m, Check check) {
  size_t cap = 0;
  {
    Store store = start;
    CrashAfter counting(store, SIZE_MAX);
    save(counting, m);
    cap = 4 * counting.spent + 4;
  }
  for (size_t budget = 0; budget <= cap; ++budget) {
    Store store = start;
    CrashAfter crashing(store, budget);
    const bool finished = save(crashing, m) == SaveResult::Ok && crashing.spent < budget;
    reboot(store);
    Model back;
    const auto loaded = load(store, back);
    if (finished) return loaded.form == StoreForm::Binary && allSections(back, m);
    if (!check(store, back)) return false;
  }
  return false;
}

// Every store a 1.3.3 save of `m` over `start` leaves, cut short after each
// write in turn, the finished one last.
template <class Store>
std::vector<Store> legacyCutStores(const Store& start, const Model& m, bool refuseWhole) {
  std::vector<Store> stores;
  for (size_t budget = 0;; ++budget) {
    Store store = start;
    CrashAfter crashing(store, budget, refuseWhole);
    const bool finished = legacySave(crashing, m) == SaveResult::Ok && crashing.spent < budget;
    reboot(store);
    stores.push_back(store);
    if (finished || budget > 400) break;
  }
  return stores;
}

// From a store holding `a`, every cut-short save of `b`, and from each store
// it leaves, every cut-short save of `c`: each load holds each section as the
// store before that save loaded it, or as saved.
template <class Store>
bool survivesCutShortStarts(const Store& holdingA, const Model& a, const Model& b,
                            const Model& c, size_t& checks) {
  return cutAtEveryWrite(holdingA, b, [&](const Store& cut, const Model& mid) {
    ++checks;
    if (!eachSectionOneOf(mid, a, b)) return false;
    return cutAtEveryWrite(cut, c, [&](const Store&, const Model& back) {
      ++checks;
      return eachSectionOneOf(back, mid, c);
    });
  });
}

// Distinct in every section, so a load shows which save each came from.
std::vector<Model> distinctShapes() {
  Model small;
  small.lamp.name = "small";
  small.lamp.brightness = 11;
  small.expressions.expressions = {tagged("pulse", 1, 1)};
  small.base.segments = {{"Base", 3, {Color(1, 1, 1, 1)}}};
  Model wide = small;
  wide.lamp.name = "wide";
  wide.lamp.brightness = 22;
  wide.base.segments = {{"Base", 3, {Color(2, 2, 2, 2)}}};
  wide.expressions.expressions = {fullExpression("pulse", 1), fullExpression("shifty", 2)};
  Model big = worstCaseModel('z');
  big.lamp.name = "big";
  big.lamp.brightness = 33;
  big.expressions.expressions.resize(3);
  Model quoted = worstCaseModel('"');
  quoted.lamp.brightness = 44;
  quoted.base.segments[0].colors[0] = Color(4, 4, 4, 4);
  quoted.expressions.expressions.resize(2);
  quoted.expressions.expressions[0].intervalMin = 4;
  Model none;
  none.lamp.name = "none";
  std::vector<Model> shapes;
  for (const Model& m : {small, wide, big, quoted, none}) shapes.push_back(canonical(m));
  return shapes;
}

// Every shape to every shape and on to every shape, cut short at every write,
// and the second save cut short from every store the first leaves.
void test_every_cut_short_store_survives_the_next_save() {
  const auto shapes = distinctShapes();
  size_t checks = 0;
  for (size_t a = 0; a < shapes.size(); ++a) {
    InMemoryConfigStore holdingA;
    TEST_ASSERT_TRUE(save(holdingA, shapes[a]) == SaveResult::Ok);
    for (size_t b = 0; b < shapes.size(); ++b) {
      for (size_t c = 0; c < shapes.size(); ++c) {
        char what[96];
        std::snprintf(what, sizeof(what), "%s -> %s -> %s", shapes[a].lamp.name.c_str(),
                      shapes[b].lamp.name.c_str(), shapes[c].lamp.name.c_str());
        TEST_ASSERT_TRUE_MESSAGE(
            survivesCutShortStarts(holdingA, shapes[a], shapes[b], shapes[c], checks), what);
      }
    }
  }
  printf("[crash] %zu cut-short loads checked\n", checks);
}

namespace {

// One store a release up to 1.3.3 left, with what it loads as.
struct LegacyStart {
  std::string what;
  InMemoryConfigStore store;
  Model loaded;
};

void addLegacyStart(std::vector<LegacyStart>& starts, const std::string& what,
                    const InMemoryConfigStore& store) {
  LegacyStart s{what, store, Model{}};
  load(s.store, s.loaded);  // as the first boot's load leaves it
  starts.push_back(std::move(s));
}

// Every form a release up to 1.3.3 left: a whole string (small, the largest
// 1.3.3 wrote, and an older release's 4,000 bytes), split at every escaped
// worst case, a whole config staged as a rest blob, and every store a 1.3.3
// save cut short leaves going whole to split, split to whole (staged) and
// split to split (its lamp section staged in "cfgl").
std::vector<LegacyStart> legacyStarts() {
  std::vector<LegacyStart> starts;
  const auto shapes = distinctShapes();
  Model whole = shapes[0];
  Model split = worstCaseModel('z');
  split.lamp.name = "split";
  split.expressions.expressions.resize(6);
  Model quoted = worstCaseModel('"');
  quoted.lamp.name = "quoted";
  {
    InMemoryConfigStore s;
    TEST_ASSERT_TRUE(legacySave(s, whole) == SaveResult::Ok);
    addLegacyStart(starts, "whole", s);
  }
  {
    InMemoryConfigStore s;
    TEST_ASSERT_TRUE(legacySave(s, largestWholeModel()) == SaveResult::Ok);
    addLegacyStart(starts, "largest whole", s);
  }
  {
    Model older = worstCaseModel('\\');
    older.lamp.name = "older";
    older.expressions.expressions.clear();
    std::string json = legacyJson(older);
    for (size_t i = 0; i < kMaxExpressions; ++i) {
      older.expressions.expressions.push_back(fullExpression("pulse", 1 + i % 3));
      if (legacyJson(older).size() > 4000) {
        older.expressions.expressions.pop_back();
        break;
      }
      json = legacyJson(older);
    }
    TEST_ASSERT_TRUE(json.size() > 3500 && json.size() <= 4000);
    InMemoryConfigStore s;
    s.write("cfg", json.c_str());
    addLegacyStart(starts, "older 4000-byte string", s);
  }
  for (char fill : {'z', '"', '\\'}) {
    Model m = worstCaseModel(fill);
    m.lamp.name = std::string("split ") + fill;
    InMemoryConfigStore s;
    TEST_ASSERT_TRUE(legacySave(s, m) == SaveResult::Ok);
    TEST_ASSERT_TRUE(s.has("xn") && s.has("ko"));
    addLegacyStart(starts, m.lamp.name, s);
  }
  {
    InMemoryConfigStore s;
    TEST_ASSERT_TRUE(legacySave(s, split) == SaveResult::Ok);
    CrashAfter refusing(s, SIZE_MAX, true);
    TEST_ASSERT_TRUE(legacySave(refusing, whole) == SaveResult::Ok);
    addLegacyStart(starts, "staged whole", s);
  }
  struct Transition {
    const char* what;
    const Model* from;
    const Model* to;
    bool refuseWhole;
  };
  for (const Transition& t : {Transition{"whole to split", &whole, &split, false},
                              Transition{"split to staged whole", &split, &whole, true},
                              Transition{"split to split", &split, &quoted, false}}) {
    InMemoryConfigStore s;
    TEST_ASSERT_TRUE(legacySave(s, *t.from) == SaveResult::Ok);
    const auto cuts = legacyCutStores(s, *t.to, t.refuseWhole);
    for (size_t i = 0; i < cuts.size(); ++i) {
      addLegacyStart(starts, std::string(t.what) + " cut at " + std::to_string(i), cuts[i]);
    }
  }
  return starts;
}

}  // namespace

// Migration from every legacy store, cut short at every write: unchanged (the
// first boot's save) every load holds the whole config as it was; edited, each
// section as it was or as saved. The finished store is binary and holds no
// legacy key, and the save from each cut store lands too.
void test_every_legacy_store_migrates_cut_at_every_write() {
  size_t checks = 0;
  size_t starts = 0;
  bool sawCfgl = false;
  for (LegacyStart& start : legacyStarts()) {
    ++starts;
    sawCfgl |= start.store.has("cfgl") && !start.store.has("cfg");
    Model edited = start.loaded;
    edited.lamp.name = "edited";
    edited.lamp.brightness ^= 1;
    edited.expressions.expressions.push_back(tagged("migrated", 2, 7));
    edited = canonical(edited);
    for (const Model* target : {&start.loaded, &edited}) {
      const bool unchanged = target == &start.loaded;
      const bool ok = cutAtEveryWrite(start.store, *target, [&](const InMemoryConfigStore& cut,
                                                               const Model& back) {
        ++checks;
        if (unchanged ? !allSections(back, start.loaded)
                      : !eachSectionOneOf(back, start.loaded, edited)) {
          return false;
        }
        InMemoryConfigStore next = cut;
        Model again;
        return save(next, edited) == SaveResult::Ok &&
               load(next, again).form == StoreForm::Binary && allSections(again, edited) &&
               !anyLegacyKey(next);
      });
      char what[96];
      std::snprintf(what, sizeof(what), "%s%s", start.what.c_str(),
                    unchanged ? "" : " (edited)");
      TEST_ASSERT_TRUE_MESSAGE(ok, what);
    }
  }
  printf("[crash] %zu legacy stores migrated, %zu cut-short loads checked\n", starts, checks);
  TEST_ASSERT_TRUE(sawCfgl);
}

// A store cut between writing the new settings blob and erasing the old one
// has both: either is whole, the load takes "st0", and the next save lands.
void test_a_store_with_both_settings_keys_loads_and_saves_on() {
  const auto shapes = distinctShapes();
  const Model& before = shapes[2];
  const Model& after = shapes[3];
  bool sawBoth = false;
  for (bool beforeInSt0 : {true, false}) {
    InMemoryConfigStore start;
    TEST_ASSERT_TRUE(save(start, before) == SaveResult::Ok);
    if (!beforeInSt0) {
      TEST_ASSERT_TRUE(save(start, after) == SaveResult::Ok);  // live in st1
    }
    TEST_ASSERT_TRUE(start.has("st0") != start.has("st1"));
    const Model& old = beforeInSt0 ? before : after;
    const Model& next = beforeInSt0 ? after : before;
    TEST_ASSERT_TRUE(cutAtEveryWrite(start, next, [&](InMemoryConfigStore& cut,
                                                     const Model& back) {
      if (!(cut.has("st0") && cut.has("st1"))) return eachSectionOneOf(back, old, next);
      sawBoth = true;
      if (!eachSectionOneOf(back, old, next)) return false;
      if (!sameSettings(back, beforeInSt0 ? old : next)) return false;
      InMemoryConfigStore copy = cut;
      Model again;
      return save(copy, next) == SaveResult::Ok && (load(copy, again), allSections(again, next)) &&
             copy.has("st0") != copy.has("st1");
    }));
  }
  TEST_ASSERT_TRUE(sawBoth);
}

struct Churn {
  size_t saves = 0;
  size_t crashChecks = 0;
  size_t followUps = 0;  // saves also cut short from every cut store
};

// Saves through every shape (empty, the largest 1.3.3 whole, every escaped
// worst case, the standard variant) in a seeded order, from a legacy store at
// its worst, with the system keys and dispositions rewritten between saves the
// way bonding, boots and dispositions churn them, and `pad` one-entry keys
// beside them. Every write must land, and every save must load back as saved.
// With `crashes`, the first save (the migration) and every `crashEvery`th are
// also cut short after each of their writes, and the first `followUpLimit` of
// them are cut short again from every store that leaves, by the next save.
bool churnFits(size_t pad, uint32_t seed, size_t rounds, bool crashes, Churn& churn,
               size_t followUpLimit = 0, size_t crashEvery = 25) {
  PagedNvs nvs;
  const auto system = systemKeys();
  for (const auto& k : system) {
    if (!nvs.putSystem(k.key, k.len)) return false;
  }
  for (size_t i = 0; i < pad; ++i) {
    if (!nvs.putSystem("pad/" + std::to_string(i), 0)) return false;
  }
  if (nvs.write("lampType", "standard") == 0) return false;
  uint8_t disp[DispositionStore::kMaxBlobLen];
  std::memset(disp, 1, sizeof(disp));
  if (nvs.writeBlob("disp", disp, sizeof(disp)) == 0) return false;
  if (legacySave(nvs, worstCaseModel('"')) != SaveResult::Ok) return false;

  Model standard = worstCaseModel();
  standard.expressions.expressions.clear();
  for (const char* type : {"glitchy", "pulse", "breathing", "shifty", "spotty", "flicker"}) {
    for (uint8_t target = 1; target <= 3; ++target) {
      standard.expressions.expressions.push_back(fullExpression(type, target));
    }
  }
  std::vector<Model> shapes;
  for (const Model& m : {Model{}, largestWholeModel(), worstCaseModel('z'), worstCaseModel('"'),
                         worstCaseModel('\\'), standard}) {
    shapes.push_back(canonical(m));
  }
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
    if (save(nvs, m) != SaveResult::Ok) return false;
    ++churn.saves;
    if (crashes && round % crashEvery == 0) {
      Model next = shapes[(round + 1) % shapes.size()];
      next.lamp.brightness = static_cast<uint8_t>(round + 128);
      const bool follow = churn.followUps < followUpLimit;
      churn.followUps += follow;
      const bool ok = cutAtEveryWrite(before, m, [&](const PagedNvs& cut, const Model& mid) {
        ++churn.crashChecks;
        if (!eachSectionOneOf(mid, old, m)) return false;
        if (!follow) return true;
        return cutAtEveryWrite(cut, next, [&](const PagedNvs&, const Model& back) {
          ++churn.crashChecks;
          return eachSectionOneOf(back, mid, next);
        });
      });
      if (!ok) return false;
    }
    Model back;
    if (load(nvs, back).form != StoreForm::Binary || !allSections(back, m)) return false;
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
  char back[3];
  TEST_ASSERT_EQUAL(0, nvs.readBlob("k", back, sizeof(back)));
  TEST_ASSERT_FALSE(nvs.has("k"));
  rebooted.boot();
  TEST_ASSERT_FALSE(rebooted.has("k"));
}

// A settings blob rewrite that fails that way leaves the live blob readable:
// the new one goes to the other key.
void test_failed_settings_rewrite_keeps_the_live_copy() {
  PagedNvs nvs;
  Model m = canonical(worstCaseModel());
  TEST_ASSERT_TRUE(save(nvs, m) == SaveResult::Ok);
  Model edited = m;
  edited.base.segments[0].colors[0] = Color(7, 7, 7, 7);
  nvs.failBlobKey = nvs.has("st0") ? "st1" : "st0";
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
// session, whichever key it was (a record, the settings blob), and the boot
// after them loads the latest: each such key's next write goes through a
// throwaway version first. Without that, the next write doubles the orphan's
// position and the boot drops the blob.
void test_saves_after_an_out_of_memory_write_land_and_survive_the_boot() {
  auto edit = [](Model& m, int i) {
    m.expressions.expressions[3].intervalMin = 100 + i;
    m.base.knockoutPixels[0] = static_cast<uint8_t>(i);
    m.base.segments[0].colors[0] = Color(i, i, i, i);
  };
  for (const char* key : {"x3", "settings"}) {
    for (int later : {1, 2, 5}) {
      for (bool pastOrphans : {true, false}) {
        if (!pastOrphans && later != 1) continue;
        PagedNvs nvs;
        nvs.pastOrphans = pastOrphans;
        Model latest = canonical(worstCaseModel());
        TEST_ASSERT_TRUE(save(nvs, latest) == SaveResult::Ok);
        const bool settings = std::strcmp(key, "settings") == 0;
        nvs.outOfMemoryKey = !settings ? key : (nvs.has("st0") ? "st1" : "st0");
        edit(latest, 1);
        TEST_ASSERT_TRUE(save(nvs, latest) == SaveResult::WriteFailed);
        bool allLanded = true;
        for (int i = 0; i < later; ++i) {
          edit(latest, 2 + i);
          const bool landed = save(nvs, latest) == SaveResult::Ok;
          if (pastOrphans) TEST_ASSERT_TRUE_MESSAGE(landed, key);
          allLanded &= landed;
        }
        nvs.boot();
        Model back;
        load(nvs, back);
        if (pastOrphans) {
          TEST_ASSERT_TRUE_MESSAGE(allSections(back, latest), key);
        } else {
          // Without it the latest is lost, at the boot or, where the save
          // reads its write back, reported by the save.
          TEST_ASSERT_FALSE_MESSAGE(allLanded && allSections(back, latest), key);
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
  keys.add("st1");
  TEST_ASSERT_TRUE(keys.holds("st1"));
  TEST_ASSERT_FALSE(keys.holds("st0"));
  for (size_t i = 0; i <= OrphanKeys::kMax; ++i) keys.add(("x" + std::to_string(i)).c_str());
  TEST_ASSERT_TRUE(keys.holds("anything"));
}

// A store an older release wrote: a whole cfg string of up to 4,000 bytes
// (the most an NVS string holds), its dispositions JSON migrated to the blob
// at load, and the system keys. `expressions` full, colour-only or
// one-colour expressions beside a plain or a fully escaped rest.
Model olderModel(size_t expressions, int kind, bool escapedRest) {
  Model m = escapedRest ? worstCaseModel('"') : Model{};
  for (auto* segs : {&m.base.segments, &m.shade.segments}) {
    for (auto& seg : *segs) seg.pin = 0;
  }
  m.expressions.expressions.clear();
  for (size_t i = 0; i < expressions; ++i) {
    ExpressionConfig e = fullExpression("pulse", 1 + i % 3);
    if (kind == 1) e.parameters = ExpressionParams{};
    if (kind == 2) e.colors.resize(1);
    m.expressions.expressions.push_back(e);
  }
  return m;
}

// The first save after the update, of `m` as it loads, beside `pad` spare
// one-entry keys. -1 when the older string would pass 4,000 bytes.
int olderFirstSave(const Model& m, size_t pad) {
  PagedNvs nvs;
  for (const auto& k : systemKeys()) nvs.putSystem(k.key, k.len);
  for (size_t i = 0; i < pad; ++i) nvs.putSystem("pad/" + std::to_string(i), 0);
  nvs.write("lampType", "standard");
  static char buf[8192];
  JsonWriter w(buf, sizeof(buf));
  legacy133::writeStoredJson(w, &m.lamp, m.base, m.shade, m.homeMode, &m.expressions);
  w.finish();
  if (!w.ok() || w.length() > 4000) return -1;
  const std::string older(buf, w.length());
  const std::string legacyDispositions(2200, 'd');
  if (nvs.write("cfg", older.c_str()) == 0 ||
      nvs.write("dispositions", legacyDispositions.c_str()) == 0) {
    return 0;
  }
  uint8_t disp[DispositionStore::kMaxBlobLen] = {1};
  if (nvs.writeBlob("disp", disp, sizeof(disp)) == 0) return 0;
  nvs.erase("dispositions");
  Model loaded;
  load(nvs, loaded);
  if (save(nvs, loaded) != SaveResult::Ok) return 0;
  Model back;
  return load(nvs, back).form == StoreForm::Binary && allSections(back, loaded) ? 1 : 0;
}

// Every older shape up to 4,000 bytes migrates with at least kMargin entries
// to spare: the new keys copy only what the string held.
void test_the_first_save_after_an_update_fits() {
  size_t tightest = SIZE_MAX;
  for (bool escaped : {false, true}) {
    for (int kind : {0, 1, 2}) {
      for (size_t n = 1; n <= kMaxExpressions; ++n) {
        const Model m = olderModel(n, kind, escaped);
        const int first = olderFirstSave(m, kMargin);
        if (first < 0) continue;
        TEST_ASSERT_EQUAL(1, first);
        size_t lo = kMargin, hi = 400;
        while (lo + 1 < hi) {
          const size_t mid = (lo + hi) / 2;
          (olderFirstSave(m, mid) == 1 ? lo : hi) = mid;
        }
        tightest = std::min(tightest, lo);
      }
    }
  }
  printf("[budget] first save after an update: %zu entries spare at the tightest\n", tightest);
}

void test_every_store_shape_survives_churn_with_margin() {
  Churn churn;
  for (uint32_t seed : {1u, 7u, 42u, 69u, 107u}) {
    TEST_ASSERT_TRUE(churnFits(kMargin, seed, 400, true, churn, seed == 1 ? 6 : 0));
  }
  printf("[budget] churn: %zu saves, %zu follow-ups, %zu crash points\n", churn.saves,
         churn.followUps, churn.crashChecks);
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
  RUN_TEST(test_a_legacy_whole_store_loads_and_migrates);
  RUN_TEST(test_a_store_saved_after_a_downgrade_loads_the_settings_blob);
  RUN_TEST(test_too_large_writes_nothing);
  RUN_TEST(test_no_record_is_orphaned_by_a_reset);
  RUN_TEST(test_delete_all_survives_a_reset);
  RUN_TEST(test_a_save_sweeps_records_past_its_count);
  RUN_TEST(test_saves_sweep_record_slots_past_the_cap);
  RUN_TEST(test_over_long_fields_cannot_fail_every_save);
  RUN_TEST(test_removed_expressions_erase_stale_records);
  RUN_TEST(test_failed_write_reports_and_keeps_the_old_config);
  RUN_TEST(test_an_unchanged_save_rewrites_no_blob);
  RUN_TEST(test_page_model_confines_a_string_to_one_page);
  RUN_TEST(test_page_model_failed_rewrite_takes_the_old_copy);
  RUN_TEST(test_failed_settings_rewrite_keeps_the_live_copy);
  RUN_TEST(test_saves_after_an_out_of_memory_write_land_and_survive_the_boot);
  RUN_TEST(test_the_orphan_list_holds_its_keys);
  RUN_TEST(test_a_key_is_written_past_its_orphan_until_boot);
  RUN_TEST(test_no_legacy_key_goes_before_the_read_back);
  RUN_TEST(test_a_mismatched_read_back_keeps_the_legacy_store);
  RUN_TEST(test_no_save_writes_a_string_over_three_bytes);
  RUN_TEST(test_a_legacy_boot_saves_once);
  RUN_TEST(test_every_cut_short_store_survives_the_next_save);
  RUN_TEST(test_every_legacy_store_migrates_cut_at_every_write);
  RUN_TEST(test_a_store_with_both_settings_keys_loads_and_saves_on);
  RUN_TEST(test_web_document_round_trip_keeps_rotation);
  RUN_TEST(test_strings_are_always_placed);
  RUN_TEST(test_small_blobs_are_always_placed);
  RUN_TEST(test_peaks_fit_the_entries);
  RUN_TEST(test_the_first_save_after_an_update_fits);
  RUN_TEST(test_every_store_shape_survives_churn_with_margin);
  RUN_TEST(test_crash_mid_save_removing_an_entry_keeps_the_rest);
  RUN_TEST(test_crash_mid_save_adding_and_editing_keeps_the_rest);
  RUN_TEST(test_crash_mid_save_growing_and_shrinking_keeps_the_rest);
  RUN_TEST(test_crash_mid_migration_keeps_the_legacy_config);
  RUN_TEST(test_load_of_a_legacy_whole_store_drops_stale_blobs);
  RUN_TEST(test_an_unreadable_legacy_rest_blob_is_reported);
  RUN_TEST(test_an_unreadable_legacy_lamp_blob_is_reported);
  RUN_TEST(test_a_legacy_split_store_missing_its_rest_is_reported);
  RUN_TEST(test_unreadable_record_drops_only_itself);
  RUN_TEST(test_unregistered_type_record_loads);
  return UNITY_END();
}
