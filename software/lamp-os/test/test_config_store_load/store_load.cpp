// Native tests for config_codec::loadConfig, the load Config's constructor
// runs: a settings blob is the authority over every legacy key beside it,
// both blob keys load "st0", an unreadable blob is reported, and a store with
// no blob loads through the legacy loader as before.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "config/config_codec.hpp"
#include "../test_config_records/config_model.hpp"
#include "../test_config_records/legacy_store.hpp"
#include "../test_config_records/paged_nvs.hpp"

#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;
using config_codec::StoreForm;

void setUp(void) {}
void tearDown(void) {}

namespace {

uint8_t s_buf[config_codec::kMaxSettingsBlobLen];

config_codec::LoadedConfig load(ConfigStore& store, Model& m) {
  return config_codec::loadConfig(store, m.lamp, m.base, m.shade, m.expressions, m.homeMode,
                                  s_buf, sizeof(s_buf));
}

// The load Config's constructor ran before the settings blob: the "cfg"
// string, then loadStoredConfig.
config_codec::LoadedRecords legacyLoad(ConfigStore& store, Model& m) {
  JsonDocument doc;
  deserializeJson(doc, store.read("cfg", "{}"));
  bool withinCaps = true;
  return config_codec::loadStoredConfig(store, doc.as<JsonObject>(), m.lamp, m.base, m.shade,
                                        m.expressions, m.homeMode, withinCaps);
}

void legacySave(ConfigStore& store, const Model& m) {
  static char buf[legacy133::kMaxStoredCfgJsonLen + 1];
  TEST_ASSERT_TRUE(legacy133::saveConfig(store, m.lamp, m.base, m.shade, m.expressions,
                                         m.homeMode, buf, sizeof(buf)) ==
                   config_codec::SaveResult::Ok);
}

std::string json(const Model& m, bool lampOnly) {
  static char buf[8192];
  JsonWriter w(buf, sizeof(buf));
  if (lampOnly) {
    legacy133::writeLampOnlyJson(w, m.lamp);
  } else {
    legacy133::writeStoredJson(w, &m.lamp, m.base, m.shade, m.homeMode, &m.expressions);
  }
  w.finish();
  TEST_ASSERT_TRUE(w.ok());
  return std::string(buf, w.length());
}

void putSettings(ConfigStore& store, const char* key, const Model& m) {
  uint8_t blob[config_codec::kMaxSettingsBlobLen];
  const size_t len = config_codec::encodeSettings(m.lamp, m.base, m.shade, m.homeMode, blob);
  TEST_ASSERT_EQUAL(len, store.writeBlob(key, blob, len));
}

// What the blob of `m` loads as, onto class defaults.
Model viaBlob(const Model& m) {
  uint8_t blob[config_codec::kMaxSettingsBlobLen];
  const size_t len = config_codec::encodeSettings(m.lamp, m.base, m.shade, m.homeMode, blob);
  Model out;
  TEST_ASSERT_TRUE(
      config_codec::decodeSettings(blob, len, out.lamp, out.base, out.shade, out.homeMode));
  return out;
}

bool sameExpressions(const Model& a, const Model& b) {
  const auto& x = a.expressions.expressions;
  const auto& y = b.expressions.expressions;
  if (x.size() != y.size()) return false;
  for (size_t i = 0; i < x.size(); ++i) {
    if (x[i].type != y[i].type || x[i].target != y[i].target || x[i].enabled != y[i].enabled ||
        x[i].intervalMin != y[i].intervalMin || x[i].intervalMax != y[i].intervalMax ||
        x[i].colors != y[i].colors || !(x[i].parameters == y[i].parameters)) {
      return false;
    }
  }
  return true;
}

Model named(Model m, const char* name, uint8_t brightness) {
  m.lamp.name = name;
  m.lamp.brightness = brightness;
  m.homeMode.ssid = name;
  return m;
}

Model smallModel() {
  Model m;
  m.expressions.expressions = {fullExpression("pulse", 1)};
  m.base.segments = {{"Base", 3, {Color(1, 2, 3, 4)}}};
  m.base.knockoutPixels = {10, 20, 30};
  return named(m, "small", 11);
}

Model splitModel() {
  Model m = worstCaseModel('z');
  m.expressions.expressions.resize(3);
  return named(m, "split", 22);
}

// Every form a release up to 1.3.3 leaves: a whole "cfg" string, a split
// store, a whole config staged as a rest blob with "cfg" erased, and a split
// store with its lamp section staged in "cfgl" and "cfg" erased.
enum class Legacy { Whole, Split, StagedWhole, LampInCfgl };
constexpr Legacy kLegacyForms[] = {Legacy::Whole, Legacy::Split, Legacy::StagedWhole,
                                   Legacy::LampInCfgl};

// The model a legacy store of `form` holds.
Model legacyModel(Legacy form) {
  return form == Legacy::Whole || form == Legacy::StagedWhole ? smallModel() : splitModel();
}

void buildLegacy(ConfigStore& store, Legacy form) {
  const Model m = legacyModel(form);
  switch (form) {
    case Legacy::Whole:
      legacySave(store, smallModel());
      TEST_ASSERT_TRUE(store.read("cfg", "").find("\"expressions\":") != std::string::npos);
      break;
    case Legacy::Split:
      legacySave(store, m);
      TEST_ASSERT_TRUE(store.has("xn") && store.has("ko"));
      break;
    case Legacy::StagedWhole: {
      legacySave(store, splitModel());
      const std::string whole = json(m, false);
      TEST_ASSERT_TRUE(store.erase("cfgb") && store.erase("cfgc") && store.erase("cfg"));
      TEST_ASSERT_TRUE(store.writeBlob("cfgb", whole.data(), whole.size()) > 0);
      break;
    }
    case Legacy::LampInCfgl: {
      legacySave(store, m);
      const std::string lamp = json(m, true);
      TEST_ASSERT_TRUE(store.writeBlob("cfgl", lamp.data(), lamp.size()) > 0);
      TEST_ASSERT_TRUE(store.erase("cfg"));
      break;
    }
  }
}

const char* kLegacyKeys[] = {"cfg", "cfgb", "cfgc", "cfgl", "ko"};
const char* kEveryKey[] = {"cfg", "cfgb", "cfgc", "cfgl", "ko", "xn", "x0", "x1", "x2", "st0",
                           "st1"};

// Class defaults as a load of an empty store leaves them.
Model loadedDefaults() {
  InMemoryConfigStore empty;
  Model m;
  legacyLoad(empty, m);
  return m;
}

bool anyLegacyKey(ConfigStore& store) {
  for (const char* key : kLegacyKeys) {
    if (store.has(key)) return true;
  }
  return false;
}

}  // namespace

// The settings blob alone loads: the load before it, which read only the
// legacy keys, served defaults for this store.
void test_a_store_holding_only_a_settings_blob_loads_it() {
  const Model stored = named(worstCaseModel('"'), "binary", 33);
  InMemoryConfigStore store;
  putSettings(store, "st0", stored);
  Model back;
  const auto loaded = load(store, back);
  TEST_ASSERT_TRUE(loaded.form == StoreForm::Binary);
  TEST_ASSERT_FALSE(loaded.records.unreadable || loaded.parseFailed);
  TEST_ASSERT_EQUAL_STRING("", firstDifference(viaBlob(stored), back).c_str());
  TEST_ASSERT_EQUAL_STRING("binary", back.lamp.name.c_str());
  Model before;
  legacyLoad(store, before);
  TEST_ASSERT_EQUAL_STRING("", firstDifference(loadedDefaults(), before).c_str());
}

// A settings blob beside any legacy store is the authority: it loads with the
// records beside it, and every legacy key goes.
void test_a_settings_blob_beside_each_legacy_store_wins_and_erases_it() {
  Model binary = named(smallModel(), "binary", 44);
  binary.base.knockoutPixels = {5, 6, 7};
  for (Legacy form : kLegacyForms) {
    InMemoryConfigStore store;
    buildLegacy(store, form);
    const bool records = store.has("xn");
    Model recordsOnly;
    config_codec::loadRecords(store, recordsOnly.base, recordsOnly.expressions, true, false);
    putSettings(store, "st0", binary);
    Model back;
    const auto loaded = load(store, back);
    TEST_ASSERT_TRUE(loaded.form == StoreForm::Binary);
    TEST_ASSERT_TRUE(sameExpressions(back, recordsOnly));
    Model expected = viaBlob(binary);
    expected.expressions = back.expressions;
    TEST_ASSERT_EQUAL_STRING("", firstDifference(expected, back).c_str());
    TEST_ASSERT_FALSE(anyLegacyKey(store));
    TEST_ASSERT_EQUAL(records, store.has("xn"));
    TEST_ASSERT_TRUE(store.has("st0"));
  }
}

// A save cut short between writing one blob key and erasing the other leaves
// both; "st0" loads, whichever is newer.
void test_both_settings_keys_load_st0() {
  const Model a = named(Model{}, "first", 1);
  const Model b = named(Model{}, "second", 2);
  for (bool aInSt0 : {true, false}) {
    InMemoryConfigStore store;
    putSettings(store, "st1", aInSt0 ? b : a);
    putSettings(store, "st0", aInSt0 ? a : b);
    Model back;
    load(store, back);
    TEST_ASSERT_EQUAL_STRING(aInSt0 ? "first" : "second", back.lamp.name.c_str());
    TEST_ASSERT_TRUE(store.has("st0") && store.has("st1"));
  }
}

// A settings chunk that fails its CRC is reported, so Config serves what is
// left without the first-boot save writing it over the store. The other key,
// when it holds a blob, loads.
void test_an_unreadable_settings_blob_is_reported() {
  const Model a = named(worstCaseModel('z'), "a", 1);
  const Model b = named(worstCaseModel('\\'), "b", 2);
  {
    PagedNvs nvs;
    putSettings(nvs, "st0", a);
    nvs.corruptBlob("st0");
    Model back;
    const auto loaded = load(nvs, back);
    TEST_ASSERT_TRUE(loaded.records.unreadable);
    TEST_ASSERT_FALSE(loaded.form == StoreForm::Binary);
    TEST_ASSERT_EQUAL_STRING("", firstDifference(loadedDefaults(), back).c_str());
  }
  {
    PagedNvs nvs;
    putSettings(nvs, "st0", a);
    putSettings(nvs, "st1", b);
    nvs.corruptBlob("st0");
    Model back;
    const auto loaded = load(nvs, back);
    TEST_ASSERT_TRUE(loaded.records.unreadable);
    TEST_ASSERT_TRUE(loaded.form == StoreForm::Binary);
    TEST_ASSERT_EQUAL_STRING("b", back.lamp.name.c_str());
  }
  {
    PagedNvs nvs;
    putSettings(nvs, "st0", a);
    Model back;
    TEST_ASSERT_FALSE(load(nvs, back).records.unreadable);
  }
}

// A blob that reads but does not decode (a format this build does not know)
// is reported too, and the legacy keys beside it load.
void test_an_undecodable_settings_blob_falls_back_to_the_legacy_store() {
  InMemoryConfigStore store;
  buildLegacy(store, Legacy::Whole);
  const uint8_t future[3] = {9, 0, 0};
  store.writeBlob("st0", future, sizeof(future));
  Model back;
  const auto loaded = load(store, back);
  TEST_ASSERT_TRUE(loaded.records.unreadable);
  TEST_ASSERT_TRUE(loaded.form == StoreForm::Whole);
  TEST_ASSERT_EQUAL_STRING("small", back.lamp.name.c_str());
  TEST_ASSERT_TRUE(store.has("cfg"));
}

// A settings blob always carries `named`; a legacy store carries it once the
// release that wrote it knew the flag.
void test_named_key_presence() {
  InMemoryConfigStore binary;
  putSettings(binary, "st0", Model{});
  Model m;
  TEST_ASSERT_TRUE(load(binary, m).namedKeyPresent);
  InMemoryConfigStore preFlag;
  preFlag.write("cfg", "{\"lamp\":{\"name\":\"jacko\"},\"expressions\":[]}");
  Model p;
  TEST_ASSERT_FALSE(load(preFlag, p).namedKeyPresent);
  for (Legacy form : kLegacyForms) {
    InMemoryConfigStore store;
    buildLegacy(store, form);
    Model l;
    TEST_ASSERT_EQUAL(form != Legacy::StagedWhole && form != Legacy::LampInCfgl,
                      load(store, l).namedKeyPresent);
  }
}

// With no settings blob, each legacy store loads exactly as the legacy load
// loads it, leaves the store as that load does, and reports its form.
void test_legacy_stores_load_as_before() {
  for (Legacy form : kLegacyForms) {
    InMemoryConfigStore viaNew;
    buildLegacy(viaNew, form);
    InMemoryConfigStore viaOld = viaNew;
    Model a, b;
    const auto loaded = load(viaNew, a);
    const auto records = legacyLoad(viaOld, b);
    TEST_ASSERT_EQUAL_STRING("", firstDifference(a, b).c_str());
    TEST_ASSERT_TRUE(sameExpressions(a, b));
    TEST_ASSERT_EQUAL_STRING(legacyModel(form).lamp.name.c_str(), a.lamp.name.c_str());
    TEST_ASSERT_EQUAL(records.present, loaded.records.present);
    TEST_ASSERT_EQUAL(records.unreadable, loaded.records.unreadable);
    for (const char* key : kEveryKey) TEST_ASSERT_EQUAL(viaOld.has(key), viaNew.has(key));
    const bool split = form == Legacy::Split || form == Legacy::LampInCfgl;
    TEST_ASSERT_TRUE(loaded.form == (split ? StoreForm::Split : StoreForm::Whole));
  }
}

void test_an_empty_store_loads_defaults() {
  InMemoryConfigStore store;
  Model back;
  const auto loaded = load(store, back);
  TEST_ASSERT_TRUE(loaded.form == StoreForm::Empty);
  TEST_ASSERT_FALSE(loaded.namedKeyPresent || loaded.parseFailed || loaded.records.unreadable);
  TEST_ASSERT_EQUAL_STRING("", firstDifference(loadedDefaults(), back).c_str());
}

// A "cfg" string that does not parse leaves the model at class defaults.
void test_an_unparseable_cfg_string_is_reported() {
  InMemoryConfigStore store;
  store.write("cfg", "{\"lamp\":{\"name\":\"jac");
  Model back;
  const auto loaded = load(store, back);
  TEST_ASSERT_TRUE(loaded.parseFailed);
  TEST_ASSERT_EQUAL_STRING("", firstDifference(Model{}, back).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_store_holding_only_a_settings_blob_loads_it);
  RUN_TEST(test_a_settings_blob_beside_each_legacy_store_wins_and_erases_it);
  RUN_TEST(test_both_settings_keys_load_st0);
  RUN_TEST(test_an_unreadable_settings_blob_is_reported);
  RUN_TEST(test_an_undecodable_settings_blob_falls_back_to_the_legacy_store);
  RUN_TEST(test_named_key_presence);
  RUN_TEST(test_legacy_stores_load_as_before);
  RUN_TEST(test_an_empty_store_loads_defaults);
  RUN_TEST(test_an_unparseable_cfg_string_is_reported);
  return UNITY_END();
}
