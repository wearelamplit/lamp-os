// The colour characteristics' write paths, over the real apply headers and a
// real Config: a bare array, the `seg` form and the object form with
// `rotate`. The render functions are stubs that record the RAM rotation and
// colours they see. No config.cpp is linked, so these paths cannot persist.

#include <unity.h>

#include <ArduinoJson.h>

#include <vector>

#include "components/apply/apply_base_colors.hpp"
#include "components/apply/apply_shade_colors.hpp"

#include "../../src/config/config_codec.cpp"
#include "../../src/config/disposition_store.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"

lamp::Config config;

using namespace lamp;

namespace {
struct RenderSeen {
  int calls = 0;
  ColorRotation rotation;
  std::vector<Color> colors;
};
RenderSeen g_shade;
RenderSeen g_base;

const ColorRotation kSet{true, 12, RotationDir::Up};
const std::vector<Color> kRed{Color(255, 0, 0, 0)};
}  // namespace

namespace lamp {
void renderShadeColors(JsonArray) {
  ++g_shade.calls;
  g_shade.rotation = ::config.shade.rotation;
  g_shade.colors = ::config.shade.broadcastColors();
}
void renderBaseColors(JsonArray) {
  ++g_base.calls;
  g_base.rotation = ::config.base.rotation;
  g_base.colors = ::config.base.broadcastColors();
}
}  // namespace lamp

void setUp() {
  ::config.shade = ShadeSettings{};
  ::config.base = BaseSettings{};
  g_shade = RenderSeen{};
  g_base = RenderSeen{};
}
void tearDown() {}

namespace {
void writeShade(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  apply::shadeColorsWriteToConfig(doc.as<JsonVariant>());
}
void writeBase(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  apply::baseColorsWriteToConfig(doc.as<JsonVariant>());
}
const std::vector<Color> kGreenBlue{Color(0, 255, 0, 0), Color(0, 0, 255, 0)};
void addSegment(std::vector<SegmentSettings>& segs) { segs.push_back({"B", 10, kRed}); }
}  // namespace

void test_bare_array_leaves_rotation_alone() {
  ::config.shade.rotation = kSet;
  ::config.base.rotation = kSet;
  writeShade("[\"#00ff0000\",\"#0000ff00\"]");
  writeBase("[\"#00ff0000\",\"#0000ff00\"]");
  TEST_ASSERT_TRUE(::config.shade.rotation == kSet);
  TEST_ASSERT_TRUE(::config.base.rotation == kSet);
  TEST_ASSERT_EQUAL(1, g_shade.calls);
  TEST_ASSERT_EQUAL(1, g_base.calls);
  TEST_ASSERT_TRUE(g_shade.colors == kGreenBlue);
  TEST_ASSERT_TRUE(g_base.colors == kGreenBlue);
}

void test_seg_form_without_rotate_leaves_rotation_alone() {
  ::config.shade.rotation = kSet;
  writeShade("{\"seg\":0,\"colors\":[\"#00ff0000\",\"#0000ff00\"]}");
  TEST_ASSERT_TRUE(::config.shade.rotation == kSet);
  TEST_ASSERT_TRUE(g_shade.colors == kGreenBlue);
}

// The render reads rotation from config, so it must land first.
void test_object_form_sets_rotation_before_the_render() {
  writeShade(
      "{\"colors\":[\"#00ff0000\",\"#0000ff00\"],\"rotate\":{\"on\":true,\"periodS\":12,"
      "\"dir\":\"up\"}}");
  writeBase(
      "{\"colors\":[\"#00ff0000\",\"#0000ff00\"],\"rotate\":{\"on\":true,\"periodS\":99,"
      "\"dir\":\"down\"}}");
  TEST_ASSERT_TRUE(g_shade.rotation == kSet);
  TEST_ASSERT_TRUE((g_base.rotation == ColorRotation{true, 99, RotationDir::Down}));
  TEST_ASSERT_TRUE(g_shade.colors == kGreenBlue);
  TEST_ASSERT_TRUE(g_base.colors == kGreenBlue);
  TEST_ASSERT_TRUE(::config.shade.rotation == kSet);
}

void test_object_form_keys_apply_one_by_one() {
  ::config.shade.rotation = kSet;
  writeShade("{\"colors\":[\"#00ff0000\"],\"rotate\":{\"periodS\":7}}");
  TEST_ASSERT_TRUE((::config.shade.rotation == ColorRotation{true, 7, RotationDir::Up}));
  writeShade("{\"seg\":0,\"colors\":[\"#00ff0000\"],\"rotate\":{\"dir\":\"down\"}}");
  TEST_ASSERT_TRUE((::config.shade.rotation == ColorRotation{true, 7, RotationDir::Down}));
}

void test_multi_segment_role_ignores_rotate_and_takes_the_colours() {
  addSegment(::config.shade.segments);
  addSegment(::config.base.segments);
  const char* json =
      "{\"colors\":[\"#00ff0000\",\"#0000ff00\"],\"rotate\":{\"on\":true,\"periodS\":12,"
      "\"dir\":\"up\"}}";
  writeShade(json);
  writeBase(json);
  TEST_ASSERT_TRUE(::config.shade.rotation == ShadeSettings::kDefaultRotation);
  TEST_ASSERT_TRUE(::config.base.rotation == BaseSettings::kDefaultRotation);
  TEST_ASSERT_TRUE(g_shade.colors == kGreenBlue);
  TEST_ASSERT_TRUE(g_base.colors == kGreenBlue);
}

void test_seg_other_than_zero_ignores_rotate() {
  writeShade("{\"seg\":1,\"colors\":[\"#00ff0000\"],\"rotate\":{\"on\":true}}");
  TEST_ASSERT_TRUE(::config.shade.rotation == ShadeSettings::kDefaultRotation);
  addSegment(::config.shade.segments);
  writeShade("{\"seg\":1,\"colors\":[\"#00ff0000\",\"#0000ff00\"],\"rotate\":{\"on\":true}}");
  TEST_ASSERT_TRUE(::config.shade.rotation == ShadeSettings::kDefaultRotation);
  TEST_ASSERT_TRUE(::config.shade.segments[1].colors == kGreenBlue);
  TEST_ASSERT_EQUAL(0, g_shade.calls);
}

// What the write stores never depends on the variant flag.
void test_flag_plays_no_part_in_the_stored_rotation() {
  for (bool flag : {false, true}) {
    setUp();
    ::config.shade.rotationSupported = ::config.base.rotationSupported = flag;
    writeShade("{\"colors\":[\"#00ff0000\"],\"rotate\":{\"on\":true,\"periodS\":12,\"dir\":\"up\"}}");
    writeBase("{\"colors\":[\"#00ff0000\"],\"rotate\":{\"on\":true,\"periodS\":12,\"dir\":\"up\"}}");
    TEST_ASSERT_TRUE(::config.shade.rotation == kSet);
    TEST_ASSERT_TRUE(::config.base.rotation == kSet);
  }
}

// Every variant that does not set the flag leaves it off.
void test_defaults_leave_the_flag_off() {
  TEST_ASSERT_FALSE(Config::Defaults{}.rotationSupported);
  TEST_ASSERT_FALSE(ShadeSettings{}.rotationSupported);
  TEST_ASSERT_FALSE(BaseSettings{}.rotationSupported);
}

// An object without a non-empty colours array is dropped whole.
void test_object_without_colours_changes_nothing() {
  for (const char* json :
       {"{\"rotate\":{\"on\":true}}", "{\"colors\":[],\"rotate\":{\"on\":true}}",
        "{\"colors\":\"#ff000000\",\"rotate\":{\"on\":true}}",
        "{\"colors\":{},\"rotate\":{\"on\":true}}", "{\"seg\":0,\"rotate\":{\"on\":true}}",
        "[]", "7"}) {
    writeShade(json);
    writeBase(json);
    TEST_ASSERT_TRUE_MESSAGE(::config.shade.rotation == ShadeSettings::kDefaultRotation, json);
    TEST_ASSERT_TRUE_MESSAGE(::config.base.rotation == BaseSettings::kDefaultRotation, json);
    TEST_ASSERT_TRUE_MESSAGE(::config.shade.broadcastColors() == ShadeSettings{}.broadcastColors(),
                             json);
    TEST_ASSERT_TRUE_MESSAGE(::config.base.broadcastColors() == BaseSettings{}.broadcastColors(),
                             json);
  }
  TEST_ASSERT_EQUAL(0, g_shade.calls);
  TEST_ASSERT_EQUAL(0, g_base.calls);
}

void test_writes_leave_nothing_to_persist() {
  writeShade("{\"colors\":[\"#00ff0000\"],\"rotate\":{\"on\":true}}");
  writeBase("[\"#00ff0000\"]");
  TEST_ASSERT_FALSE(::config.persistPending());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bare_array_leaves_rotation_alone);
  RUN_TEST(test_seg_form_without_rotate_leaves_rotation_alone);
  RUN_TEST(test_object_form_sets_rotation_before_the_render);
  RUN_TEST(test_object_form_keys_apply_one_by_one);
  RUN_TEST(test_multi_segment_role_ignores_rotate_and_takes_the_colours);
  RUN_TEST(test_seg_other_than_zero_ignores_rotate);
  RUN_TEST(test_flag_plays_no_part_in_the_stored_rotation);
  RUN_TEST(test_defaults_leave_the_flag_off);
  RUN_TEST(test_object_without_colours_changes_nothing);
  RUN_TEST(test_writes_leave_nothing_to_persist);
  return UNITY_END();
}
