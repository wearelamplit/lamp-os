// Native tests for loading a "cfg" string the C firmware wrote: colours
// moved under the gamma the LED output applies, base stops and knockout
// mirrored for a reversed strip, {p, b} knockout pairs, the lamp-level home
// mode keys, the shapes without an expressions array, and a save cut short
// over it. A mesh-era store loads as before.

#include <unity.h>

#include <ArduinoJson.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "config/config_codec.hpp"
#include "config/config_store.hpp"
#include "core/hw_config.hpp"
#include "util/gamma.hpp"
#include "../test_config_records/config_model.hpp"

#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;
using config_codec::StoreForm;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Lamp "long" as the C firmware stored it, its password replaced.
const char* kLong =
    R"({"lamp":{"name":"long","brightness":53,"homeMode":false,"homeModeSSID":"",)"
    R"("homeModeBrightness":80,"password":"dummy-pw"},"base":{"px":50,"ac":2,)"
    R"("colors":["#40000d00","#3b000012","#781e050c","#2a000600","#0e001800"],)"
    R"("knockout":[{"p":33,"b":37},{"p":34,"b":27},{"p":35,"b":45},{"p":36,"b":36},)"
    R"({"p":37,"b":7},{"p":38,"b":5},{"p":39,"b":10},{"p":40,"b":25},{"p":41,"b":49},)"
    R"({"p":42,"b":36},{"p":43,"b":57},{"p":44,"b":46},{"p":45,"b":30},{"p":46,"b":75}]},)"
    R"("shade":{"px":38,"colors":["#46006400"]},"expressions":[{"type":"pulse",)"
    R"("enabled":true,"intervalMin":300,"intervalMax":900,"target":2,"pulseSpeed":3,)"
    R"("colors":["#2c00003d","#00ff0000"]}]})";

// Channels where a lower-bound or floor search misses the nearest input by 2
// (189, 200, 207, 216, 221, 228, 233, 238), a two-stop shade, home mode on.
// px 35 with knockout at p 40 (past the strip), b over 100 and p 3 twice.
const char* kHome =
    R"({"lamp":{"name":"den","brightness":70,"homeMode":true,"homeModeSSID":"home-net",)"
    R"("homeModeBrightness":35},"base":{"px":35,"ac":0,)"
    R"("colors":["#bdc8cfd8","#dde4e9ee","#01020304","#42434445"],)"
    R"("knockout":[{"p":3,"b":90},{"p":0,"b":12},{"p":40,"b":20},{"p":34,"b":130},)"
    R"({"p":20,"b":2000000000},{"p":3,"b":60}]},)"
    R"("shade":{"px":32,"colors":["#ff800000","#00102030"]},"expressions":[]})";

// The first shipped shape: no home SSID or brightness, no expressions array.
const char* kSeptember =
    R"({"lamp":{"name":"sept","brightness":40,"homeMode":true},"base":{"px":36,)"
    R"("ac":0,"colors":["#40000d00","#781e050c"],"knockout":[{"p":1,"b":50}]},)"
    R"("shade":{"px":32,"colors":["#46006400"]}})";

uint8_t s_buf[config_codec::kMaxSettingsBlobLen];

config_codec::LoadedConfig load(ConfigStore& store, Model& m, StripWinding winding) {
  return config_codec::loadConfig(store, m.lamp, m.base, m.shade, m.expressions, m.homeMode,
                                  s_buf, sizeof(s_buf), winding);
}

Model loadCfg(const char* json, StripWinding winding,
              config_codec::LoadedConfig* loaded = nullptr) {
  InMemoryConfigStore store;
  store.write("cfg", json);
  Model m;
  const config_codec::LoadedConfig l = load(store, m, winding);
  if (loaded) *loaded = l;
  return m;
}

JsonDocument parse(const char* json) {
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
  return doc;
}

std::vector<Color> hexList(JsonArrayConst arr) {
  std::vector<Color> out;
  for (JsonVariantConst c : arr) out.push_back(hexStringToColor(c.as<const char*>()));
  return out;
}

int gammaDistance(uint8_t s, uint8_t v) {
  return std::abs(int(Adafruit_NeoPixel::gamma8(s)) - int(v));
}

// gamma8 of `loaded` is as near `old` as gamma8 of any byte gets.
void assertChannelNearest(uint8_t loaded, uint8_t old) {
  for (int s = 0; s < 256; ++s) {
    TEST_ASSERT_TRUE_MESSAGE(gammaDistance(uint8_t(s), old) >= gammaDistance(loaded, old),
                             "a nearer channel value exists");
  }
}

void assertColorNearest(const Color& loaded, const Color& old) {
  assertChannelNearest(loaded.r, old.r);
  assertChannelNearest(loaded.g, old.g);
  assertChannelNearest(loaded.b, old.b);
  assertChannelNearest(loaded.w, old.w);
}

// `loaded` is `old` with each colour migrated, in reverse order when reversed.
void assertStops(const std::vector<Color>& loaded, const std::vector<Color>& old, bool reversed) {
  TEST_ASSERT_EQUAL(old.size(), loaded.size());
  for (size_t i = 0; i < old.size(); ++i) {
    assertColorNearest(loaded[i], old[reversed ? old.size() - 1 - i : i]);
  }
}

// The knockout level whose gamma-corrected scale is nearest old C's raw b%.
uint8_t expectedLevel(int b) {
  const int target = (255 * std::min(b, 100) + 50) / 100;
  int best = 0;
  int bestErr = 1 << 30;
  for (int k = 0; k <= 100; ++k) {
    const int err =
        std::abs(int(Adafruit_NeoPixel::gamma8(uint8_t((255 * k + 50) / 100))) - target);
    if (err < bestErr) {
      best = k;
      bestErr = err;
    }
  }
  return uint8_t(best);
}

const StripWinding kWindings[] = {{false, false}, {true, false}, {false, true}, {true, true}};

}  // namespace

void test_colours_and_stop_order_migrate() {
  for (const char* json : {kLong, kHome, kSeptember}) {
    JsonDocument doc = parse(json);
    for (const StripWinding w : kWindings) {
      const Model m = loadCfg(json, w);
      assertStops(m.base.broadcastColors(), hexList(doc["base"]["colors"]), w.base);
      assertStops(m.shade.broadcastColors(), hexList(doc["shade"]["colors"]), w.shade);
      JsonArrayConst exprs = doc["expressions"];
      TEST_ASSERT_EQUAL(exprs.size(), m.expressions.expressions.size());
      for (size_t i = 0; i < exprs.size(); ++i) {
        assertStops(m.expressions.expressions[i].colors, hexList(exprs[i]["colors"]), false);
      }
    }
  }
}

void test_knockout_pairs_land_on_their_pixel() {
  struct Case {
    const char* json;
    size_t px;
  };
  for (const Case c : {Case{kLong, 50}, Case{kHome, 35}, Case{kSeptember, 36}}) {
    JsonDocument doc = parse(c.json);
    for (const StripWinding w : kWindings) {
      const Model m = loadCfg(c.json, w);
      std::vector<uint8_t> expected(c.px, 100);
      for (JsonObjectConst pair : doc["base"]["knockout"].as<JsonArrayConst>()) {
        const size_t p = pair["p"].as<size_t>();
        if (p >= c.px) continue;
        expected[w.base ? c.px - 1 - p : p] = expectedLevel(pair["b"].as<int>());
      }
      TEST_ASSERT_EQUAL(c.px, m.base.knockoutPixels.size());
      TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), m.base.knockoutPixels.data(), c.px);
    }
  }
  // The repeated p 3 keeps its last level, and b 130 loads as 100% would.
  const Model m = loadCfg(kHome, {});
  TEST_ASSERT_EQUAL_UINT8(expectedLevel(60), m.base.knockoutPixels[3]);
  TEST_ASSERT_EQUAL_UINT8(100, m.base.knockoutPixels[34]);
}

void test_lamp_home_mode_keys_migrate() {
  const Model on = loadCfg(kHome, {});
  TEST_ASSERT_TRUE(on.homeMode.enabled);
  TEST_ASSERT_TRUE(on.homeMode.networkBound);
  TEST_ASSERT_EQUAL_STRING("home-net", on.homeMode.ssid.c_str());
  TEST_ASSERT_EQUAL_UINT8(35, on.homeMode.brightness);

  std::string anywhere(kHome);
  anywhere.replace(anywhere.find("home-net"), 8, "");
  const Model always = loadCfg(anywhere.c_str(), {});
  TEST_ASSERT_TRUE(always.homeMode.enabled);
  TEST_ASSERT_FALSE(always.homeMode.networkBound);
  TEST_ASSERT_TRUE(always.homeMode.ssid.empty());

  const Model off = loadCfg(kLong, {});
  TEST_ASSERT_FALSE(off.homeMode.enabled);
  TEST_ASSERT_FALSE(off.homeMode.networkBound);
  TEST_ASSERT_EQUAL_UINT8(80, off.homeMode.brightness);
  // Old C suppressed social and glitchy at home, as the defaults do.
  TEST_ASSERT_TRUE(off.homeMode.socialDisabled);
  TEST_ASSERT_EQUAL(1, off.homeMode.disabledExpressionTypes.size());
  TEST_ASSERT_EQUAL_STRING("glitchy", off.homeMode.disabledExpressionTypes[0].c_str());
}

void test_september_shape_loads_whole() {
  config_codec::LoadedConfig loaded;
  const Model m = loadCfg(kSeptember, {true, false}, &loaded);
  TEST_ASSERT_TRUE(loaded.form == StoreForm::Whole);
  TEST_ASSERT_FALSE(loaded.parseFailed);
  TEST_ASSERT_FALSE(loaded.records.unreadable);
  TEST_ASSERT_FALSE(loaded.namedKeyPresent);
  TEST_ASSERT_TRUE(m.homeMode.enabled);
  TEST_ASSERT_FALSE(m.homeMode.networkBound);
  TEST_ASSERT_EQUAL_UINT8(80, m.homeMode.brightness);
  TEST_ASSERT_EQUAL_STRING("sept", m.lamp.name.c_str());
  TEST_ASSERT_EQUAL_UINT8(40, m.lamp.brightness);
  TEST_ASSERT_TRUE(m.expressions.expressions.empty());
}

// A store with no colours for a role keeps that role's class default as is.
void test_role_without_colours_keeps_the_class_default() {
  const char* json =
      R"({"lamp":{"name":"bare","homeMode":false},"base":{"px":36},)"
      R"("shade":{"px":32,"colors":["#46006400"]},"expressions":[]})";
  for (const StripWinding w : kWindings) {
    const Model m = loadCfg(json, w);
    TEST_ASSERT_EQUAL(1, m.base.broadcastColors().size());
    TEST_ASSERT_TRUE(m.base.broadcastColors()[0] == kBaseDefaultColor);
  }
}

// A mesh-era whole "cfg" (no lamp.homeMode) loads what fromJson loads,
// whatever the winding.
void test_mesh_era_store_is_untouched() {
  Model src;
  src.lamp.name = "mesh";
  src.lamp.named = true;
  src.base.broadcastColors() = {Color(0x40, 0x0d, 0x78, 0x05), Color(0x2a, 0x06, 0x0e, 0x18),
                                Color(0xbd, 0xc8, 0xcf, 0xd8)};
  src.shade.broadcastColors() = {Color(0x46, 0x00, 0x64, 0x00), Color(0x12, 0x34, 0x56, 0x78)};
  src.base.segments.front().px = 6;
  src.base.knockoutPixels = {10, 100, 37, 100, 75, 3};
  src.homeMode.enabled = true;
  src.homeMode.ssid = "mesh-net";
  src.homeMode.brightness = 41;
  ExpressionConfig e;
  e.type = "pulse";
  e.target = 2;
  e.colors = {Color(0x2c, 0x00, 0x00, 0x3d)};
  src.expressions.expressions.push_back(e);
  const std::string json = webDocument(src);

  JsonDocument doc = parse(json.c_str());
  Model expected;
  config_codec::fromJson(doc.as<JsonObject>(), expected.lamp, expected.base, expected.shade,
                         expected.expressions, expected.homeMode);
  for (const StripWinding w : kWindings) {
    const Model m = loadCfg(json.c_str(), w);
    TEST_ASSERT_EQUAL_STRING("", firstDifference(m, expected).c_str());
  }
}

namespace {

bool hasSettingsBlob(InMemoryConfigStore& store) { return store.has("st0") || store.has("st1"); }

// Saves `edit` over long's store cut short after each write in turn. Each
// reload is long's plain migration while no settings blob has landed, and the
// edit once one has; after the save completes "cfg" is gone.
void cutEverySave(const Model& migrated, const Model& edit, StripWinding w) {
  bool completed = false;
  for (size_t writes = 0; writes < 64 && !completed; ++writes) {
    InMemoryConfigStore store;
    store.write("cfg", kLong);
    store.writesBeforeCrash = writes;
    completed = config_codec::saveConfig(store, edit.lamp, edit.base, edit.shade,
                                         edit.expressions, edit.homeMode, s_buf,
                                         sizeof(s_buf)) == config_codec::SaveResult::Ok;
    store.writesBeforeCrash = SIZE_MAX;
    const bool landed = hasSettingsBlob(store);
    Model reloaded;
    load(store, reloaded, w);
    TEST_ASSERT_EQUAL_STRING("", firstDifference(reloaded, landed ? edit : migrated).c_str());
    if (completed) {
      TEST_ASSERT_FALSE(store.has("cfg"));
      Model again;
      TEST_ASSERT_TRUE(load(store, again, w).form == StoreForm::Binary);
      TEST_ASSERT_EQUAL_STRING("", firstDifference(again, edit).c_str());
    }
  }
  TEST_ASSERT_TRUE(completed);
}

}  // namespace

void test_save_cut_short_keeps_the_old_store_authoritative() {
  const StripWinding w{true, false};
  const Model migrated = loadCfg(kLong, w);
  cutEverySave(migrated, migrated, w);

  Model edit = migrated;
  edit.expressions.expressions.front().enabled = false;
  edit.expressions.expressions.front().colors.front() = Color(9, 8, 7, 6);
  edit.base.broadcastColors().front() = Color(1, 2, 3, 4);
  cutEverySave(migrated, edit, w);
}

void test_winding_reads_the_first_strip_of_each_role() {
  HwConfig shadeFirst{{{Surface::Shade, 25, ByteOrder::GRBW},
                       {Surface::Base, 26, ByteOrder::GRBW, 0, nullptr, 0, true}}};
  StripWinding w = windingOf(shadeFirst);
  TEST_ASSERT_TRUE(w.base);
  TEST_ASSERT_FALSE(w.shade);

  HwConfig baseFirst{{{Surface::Base, 26, ByteOrder::GRBW, 0, nullptr, 0, false},
                      {Surface::Base, 27, ByteOrder::GRBW, 0, nullptr, 0, true},
                      {Surface::Shade, 25, ByteOrder::GRBW, 0, nullptr, 0, true}}};
  w = windingOf(baseFirst);
  TEST_ASSERT_FALSE(w.base);
  TEST_ASSERT_TRUE(w.shade);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_colours_and_stop_order_migrate);
  RUN_TEST(test_knockout_pairs_land_on_their_pixel);
  RUN_TEST(test_lamp_home_mode_keys_migrate);
  RUN_TEST(test_september_shape_loads_whole);
  RUN_TEST(test_role_without_colours_keeps_the_class_default);
  RUN_TEST(test_mesh_era_store_is_untouched);
  RUN_TEST(test_save_cut_short_keeps_the_old_store_authoritative);
  RUN_TEST(test_winding_reads_the_first_strip_of_each_role);
  return UNITY_END();
}
