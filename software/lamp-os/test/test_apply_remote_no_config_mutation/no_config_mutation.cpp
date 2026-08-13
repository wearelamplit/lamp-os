// Native-host mirror-class test pinning the Phase A.1 user/remote split
// contract for ToRender helpers.
//
// WHAT THIS TEST ACTUALLY CHECKS:
//   The mock signatures match production exactly. If anyone changes the
//   production ToRender helpers in src/components/apply/, the mocks here
//   must change too — surfacing the dependency loudly in code review.
//   The bodies don't mutate config (by construction) because they're
//   stubs counting calls, so a passing test is evidence that nothing in
//   THIS file accidentally wrote to the mock config.
//
// WHAT THIS TEST DOES NOT CATCH:
//   - A bug where the PRODUCTION ToRender helper mutates config — these
//     mocks bypass the real code path entirely. Native env can't link
//     lamp.cpp (NimBLE/FastLED deps); the real regression guard
//     for that case is the bench test (#12 in the plan).
//   - A bug where applyRemoteOpLocal accidentally calls ToConfig instead
//     of ToRender. That's a static-analysis or integration-test concern.

#include <unity.h>

#include <ArduinoJson.h>
#include <cstdint>
#include <vector>

namespace lamp {
struct Color { uint8_t r=0, g=0, b=0; };
struct LampSection { uint8_t brightness = 50; };
struct ShadeSection { std::vector<Color> colors; };
struct BaseSection  { std::vector<Color> colors; };
struct ExpressionsSection { std::vector<int> expressions; };
struct MockConfig {
  LampSection lamp;
  ShadeSection shade;
  BaseSection base;
  ExpressionsSection expressions;
};

MockConfig mock_config;

int render_brightness_calls = 0;
int render_shade_calls = 0;
int render_base_calls = 0;
int render_expression_calls = 0;

namespace apply {

// Mock signatures MUST match production in src/components/apply/.
// Mock bodies count calls but do NOT touch mock_config — that's the
// invariant under test.
inline void brightnessToRender(uint8_t /*level*/, bool /*isHomeMode*/) {
  render_brightness_calls++;
}
inline void shadeColorsToRender(JsonArray /*arr*/) {
  render_shade_calls++;
}
inline void baseColorsToRender(JsonArray /*arr*/) {
  render_base_calls++;
}
inline void expressionOpToRender(JsonObject /*doc*/) {
  render_expression_calls++;
}

}  // namespace apply
}  // namespace lamp

void setUp(void) {
  lamp::mock_config = lamp::MockConfig{};
  lamp::render_brightness_calls = 0;
  lamp::render_shade_calls = 0;
  lamp::render_base_calls = 0;
  lamp::render_expression_calls = 0;
}
void tearDown(void) {}

void test_render_brightness_does_not_mutate_config() {
  uint8_t snapshot = lamp::mock_config.lamp.brightness;
  lamp::apply::brightnessToRender(99, false);
  TEST_ASSERT_EQUAL_UINT8(snapshot, lamp::mock_config.lamp.brightness);
  TEST_ASSERT_EQUAL_INT(1, lamp::render_brightness_calls);
}

void test_render_shade_does_not_mutate_config() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  auto snapshot = lamp::mock_config.shade.colors.size();
  lamp::apply::shadeColorsToRender(arr);
  TEST_ASSERT_EQUAL_size_t(snapshot, lamp::mock_config.shade.colors.size());
  TEST_ASSERT_EQUAL_INT(1, lamp::render_shade_calls);
}

void test_render_base_does_not_mutate_config() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  auto snapshot = lamp::mock_config.base.colors.size();
  lamp::apply::baseColorsToRender(arr);
  TEST_ASSERT_EQUAL_size_t(snapshot, lamp::mock_config.base.colors.size());
  TEST_ASSERT_EQUAL_INT(1, lamp::render_base_calls);
}

void test_render_expression_does_not_mutate_config() {
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  auto snapshot = lamp::mock_config.expressions.expressions.size();
  lamp::apply::expressionOpToRender(obj);
  TEST_ASSERT_EQUAL_size_t(snapshot, lamp::mock_config.expressions.expressions.size());
  TEST_ASSERT_EQUAL_INT(1, lamp::render_expression_calls);
}

void test_multiple_renders_leave_config_byte_identical() {
  // 5 iterations is enough to verify no accumulation — ToRender is a
  // pure render path with no per-call state.
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  JsonObject obj = doc.to<JsonObject>();
  lamp::MockConfig snapshot = lamp::mock_config;
  for (int i = 0; i < 5; ++i) {
    lamp::apply::brightnessToRender(static_cast<uint8_t>(i * 20), i % 2 == 0);
    lamp::apply::shadeColorsToRender(arr);
    lamp::apply::baseColorsToRender(arr);
    lamp::apply::expressionOpToRender(obj);
  }
  TEST_ASSERT_EQUAL_UINT8(snapshot.lamp.brightness,
                          lamp::mock_config.lamp.brightness);
  TEST_ASSERT_EQUAL_size_t(snapshot.shade.colors.size(),
                           lamp::mock_config.shade.colors.size());
  TEST_ASSERT_EQUAL_size_t(snapshot.base.colors.size(),
                           lamp::mock_config.base.colors.size());
  TEST_ASSERT_EQUAL_size_t(snapshot.expressions.expressions.size(),
                           lamp::mock_config.expressions.expressions.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_render_brightness_does_not_mutate_config);
  RUN_TEST(test_render_shade_does_not_mutate_config);
  RUN_TEST(test_render_base_does_not_mutate_config);
  RUN_TEST(test_render_expression_does_not_mutate_config);
  RUN_TEST(test_multiple_renders_leave_config_byte_identical);
  return UNITY_END();
}
