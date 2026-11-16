// Native tests for applyRoleDefaults, the role step of Config::applyDefaults:
// a variant's colours seed only a lamp whose store held no config, so a
// stored colour equal to the class default (white shade, purple base) is
// kept, over every store form the load reports.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "config/config.hpp"
#include "config/config_codec.hpp"
#include "../test_config_records/config_model.hpp"
#include "../test_config_records/legacy_store.hpp"

#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/config/config_codec.cpp"
#include "../../src/config/config_defaults.cpp"

using namespace lamp;
using config_codec::StoreForm;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Standard's shape: shade hex only, base left to the class default.
Config::Defaults standardLike() {
  Config::Defaults d;
  d.shadeColor = "#5A170000";
  d.basePx = 35;
  d.shadePx = 38;
  return d;
}

// Both roles scalar, neither hex equal to its class default.
Config::Defaults scalarBoth() {
  Config::Defaults d;
  d.baseColor = "#01020304";
  d.shadeColor = "#5A170000";
  d.basePx = 36;
  d.shadePx = 32;
  return d;
}

// Snafu's shape (one base segment) and lioness's (two), on either role.
Config::Defaults segmented() {
  Config::Defaults d;
  d.baseSegments = {{"Stem", 24, "#10203040"}};
  d.shadeSegments = {{"Main", 32, "#11223344,#55667788"}, {"Heads", 18, "#99aabbcc"}};
  return d;
}

std::vector<SegmentSettings> seeded(const std::vector<Config::SegmentDefault>& seeds) {
  std::vector<SegmentSettings> out;
  for (const auto& s : seeds) out.push_back({s.name, s.px, parseColorList(s.colors)});
  return out;
}

void assertSegments(const std::vector<SegmentSettings>& want,
                    const std::vector<SegmentSettings>& got) {
  TEST_ASSERT_EQUAL(want.size(), got.size());
  for (size_t i = 0; i < want.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(want[i].name.c_str(), got[i].name.c_str());
    TEST_ASSERT_EQUAL_UINT8(want[i].px, got[i].px);
    TEST_ASSERT_TRUE(want[i].colors == got[i].colors);
  }
}

uint8_t s_buf[config_codec::kMaxSettingsBlobLen];

StoreForm load(ConfigStore& store, Model& m) {
  return config_codec::loadConfig(store, m.lamp, m.base, m.shade, m.expressions, m.homeMode,
                                  s_buf, sizeof(s_buf), {})
      .form;
}

void putSettings(ConfigStore& store, const Model& m) {
  const size_t len = config_codec::encodeSettings(m.lamp, m.base, m.shade, m.homeMode, s_buf);
  TEST_ASSERT_EQUAL(len, store.writeBlob("st0", s_buf, len));
}

// A stored lamp holding the class defaults on both roles.
Model factoryColoured() {
  Model m;
  m.lamp.name = "white";
  m.lamp.named = true;
  m.base.segments.front().px = 40;
  m.shade.segments.front().px = 30;
  m.base.knockoutPixels.assign(40, 100);
  m.base.knockoutPixels[7] = 20;
  TEST_ASSERT_TRUE(m.shade.broadcastColors() ==
                   std::vector<Color>{kShadeDefaultColor});
  TEST_ASSERT_TRUE(m.base.broadcastColors() == std::vector<Color>{kBaseDefaultColor});
  return m;
}

// Loads `store`, applies `d`, and checks each role kept what it loaded.
void assertKept(ConfigStore& store, const Config::Defaults& d, StoreForm expectedForm) {
  Model m;
  TEST_ASSERT_TRUE(load(store, m) == expectedForm);
  Model loaded = m;
  loaded.shade.segments.front().px =
      resolveConfiguredPx(loaded.shade.segments.front().px, d.shadePx);
  applyRoleDefaults(d, expectedForm, m.base, m.shade);
  assertSegments(loaded.base.segments, m.base.segments);
  assertSegments(loaded.shade.segments, m.shade.segments);
}

}  // namespace

void test_fresh_store_takes_the_variant_colours() {
  for (const Config::Defaults& d : {standardLike(), scalarBoth()}) {
    InMemoryConfigStore store;
    Model m;
    TEST_ASSERT_TRUE(load(store, m) == StoreForm::Empty);
    applyRoleDefaults(d, StoreForm::Empty, m.base, m.shade);
    const std::vector<Color> base =
        d.baseColor.empty() ? std::vector<Color>{kBaseDefaultColor} : parseColorList(d.baseColor);
    TEST_ASSERT_TRUE(m.base.broadcastColors() == base);
    TEST_ASSERT_TRUE(m.shade.broadcastColors() == parseColorList(d.shadeColor));
    TEST_ASSERT_EQUAL_UINT8(d.basePx, m.base.sumPx());
    TEST_ASSERT_EQUAL_UINT8(d.shadePx, m.shade.sumPx());
    TEST_ASSERT_EQUAL(d.basePx, m.base.knockoutPixels.size());
    TEST_ASSERT_TRUE(colorsAreFactoryDefault(m.base.broadcastColors(),
                                             m.shade.broadcastColors(), d.baseColor,
                                             d.shadeColor));
  }
  InMemoryConfigStore store;
  Model m;
  load(store, m);
  const Config::Defaults d = segmented();
  applyRoleDefaults(d, StoreForm::Empty, m.base, m.shade);
  assertSegments(seeded(d.baseSegments), m.base.segments);
  assertSegments(seeded(d.shadeSegments), m.shade.segments);
  TEST_ASSERT_EQUAL(24, m.base.knockoutPixels.size());
}

void test_binary_store_keeps_factory_coloured_roles() {
  for (const Config::Defaults& d : {standardLike(), scalarBoth(), segmented()}) {
    InMemoryConfigStore store;
    putSettings(store, factoryColoured());
    assertKept(store, d, StoreForm::Binary);
  }
  // The roll sees the stored white as a configured colour.
  Model m;
  InMemoryConfigStore store;
  putSettings(store, factoryColoured());
  applyRoleDefaults(standardLike(), load(store, m), m.base, m.shade);
  TEST_ASSERT_FALSE(colorsAreFactoryDefault(m.base.broadcastColors(),
                                            m.shade.broadcastColors(), "", "#5A170000"));
}

void test_binary_store_keeps_any_other_colours() {
  Model m = factoryColoured();
  m.base.broadcastColors() = {Color(1, 2, 3, 4), Color(5, 6, 7, 8)};
  m.shade.segments = {{"Main", 20, {Color(9, 9, 9, 9)}}, {"Heads", 12, {Color(0, 0, 0, 1)}}};
  for (const Config::Defaults& d : {standardLike(), scalarBoth(), segmented()}) {
    InMemoryConfigStore store;
    putSettings(store, m);
    assertKept(store, d, StoreForm::Binary);
  }
}

void test_legacy_stores_keep_factory_coloured_roles() {
  static char buf[legacy133::kMaxStoredCfgJsonLen + 1];
  Model whole = factoryColoured();
  Model split = worstCaseModel('z');
  split.base.segments = whole.base.segments;
  split.shade.segments = whole.shade.segments;
  split.base.knockoutPixels = whole.base.knockoutPixels;
  for (const Config::Defaults& d : {standardLike(), scalarBoth(), segmented()}) {
    InMemoryConfigStore wholeStore;
    TEST_ASSERT_TRUE(legacy133::saveConfig(wholeStore, whole.lamp, whole.base, whole.shade,
                                           whole.expressions, whole.homeMode, buf,
                                           sizeof(buf)) == config_codec::SaveResult::Ok);
    assertKept(wholeStore, d, StoreForm::Whole);

    InMemoryConfigStore splitStore;
    TEST_ASSERT_TRUE(legacy133::saveConfig(splitStore, split.lamp, split.base, split.shade,
                                           split.expressions, split.homeMode, buf,
                                           sizeof(buf)) == config_codec::SaveResult::Ok);
    assertKept(splitStore, d, StoreForm::Split);

    // The C firmware's default shade.
    InMemoryConfigStore oldC;
    oldC.write("cfg",
               R"({"lamp":{"name":"c","homeMode":false},"base":{"px":40,)"
               R"("colors":["#30078300"]},"shade":{"px":30,"colors":["#000000ff"]}})");
    assertKept(oldC, d, StoreForm::Whole);
  }
}

// A store that held data but no readable colour seeds as a fresh one does.
void test_unreadable_stores_take_the_variant_colours() {
  const Config::Defaults d = scalarBoth();
  std::vector<InMemoryConfigStore> stores(3);
  stores[0].write("cfg", "{\"lamp\":");
  const uint8_t junk[] = {0xff, 0x01, 0x02};
  stores[1].writeBlob("st0", junk, sizeof(junk));
  ExpressionSettings expressions;
  expressions.expressions.push_back(fullExpression("pulse", 2));
  TEST_ASSERT_TRUE(config_codec::writeRecords(stores[2], expressions));
  for (InMemoryConfigStore& store : stores) {
    Model m;
    TEST_ASSERT_TRUE(load(store, m) == StoreForm::Empty);
    applyRoleDefaults(d, StoreForm::Empty, m.base, m.shade);
    TEST_ASSERT_TRUE(m.base.broadcastColors() == parseColorList(d.baseColor));
    TEST_ASSERT_TRUE(m.shade.broadcastColors() == parseColorList(d.shadeColor));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_store_takes_the_variant_colours);
  RUN_TEST(test_binary_store_keeps_factory_coloured_roles);
  RUN_TEST(test_binary_store_keeps_any_other_colours);
  RUN_TEST(test_legacy_stores_keep_factory_coloured_roles);
  RUN_TEST(test_unreadable_stores_take_the_variant_colours);
  return UNITY_END();
}
