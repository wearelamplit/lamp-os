// Native-host mirror-class test for apply::brightnessImmediate.
//
// WHAT THIS TEST ACTUALLY CHECKS:
//   The contract of apply::brightnessImmediate — the settings_blob
//   brightness path that skips the slider micro-fade. Mock mirrors the
//   production signature in apply_brightness.hpp. If production changes,
//   the mock must change too — same mirror-class discipline as
//   test_apply_remote_no_config_mutation.
//
// Invariants under test:
//   1. brightnessImmediate(level, isHomeMode=false) updates
//      mock_config.lamp.brightness to `level`.
//   2. brightnessImmediate(level, isHomeMode=true) updates
//      mock_config.homeMode.brightness instead.
//   3. brightnessImmediate clears the slider fade-seeded flag so the
//      next live-preview slider drag re-seeds cleanly.
//   4. brightnessImmediate drives applyEffectiveBrightness exactly
//      once per call (no double-apply, no skip).
//
// WHAT THIS TEST DOES NOT CATCH:
//   - The production brightnessImmediate diverging from the mock — the
//     real regression guard for that is the bench test (#4 in the plan).

#include <unity.h>

#include <cstdint>

namespace lamp {
struct LampSection     { uint8_t brightness = 0; };
struct HomeModeSection { uint8_t brightness = 0; };
struct MockConfig {
  LampSection lamp;
  HomeModeSection homeMode;
};
MockConfig mock_config;

// Slider fade-triple mock — production lives in lamp.cpp as
// file-statics, exposed via the brightnessFadeSeeded() / setBrightnessFade
// / clearBrightnessFadeSeed accessors in apply_brightness.hpp.
bool s_userBrightnessSeeded_mock = false;

// Mock applyEffectiveBrightness — counted, no-op.
int effective_brightness_calls = 0;

namespace apply {

// Mirror of production brightnessImmediate. MUST match the production
// inline definition in src/components/apply/apply_brightness.hpp.
inline void brightnessImmediate(uint8_t level, bool isHomeMode, uint8_t maxBrightness) {
  (void)maxBrightness;
  if (isHomeMode) {
    mock_config.homeMode.brightness = level;
  } else {
    mock_config.lamp.brightness = level;
  }
  s_userBrightnessSeeded_mock = false;
  effective_brightness_calls++;
}

}  // namespace apply
}  // namespace lamp

void setUp(void) {
  lamp::mock_config = lamp::MockConfig{};
  lamp::s_userBrightnessSeeded_mock = false;
  lamp::effective_brightness_calls = 0;
}
void tearDown(void) {}

void test_immediate_updates_lamp_brightness_when_not_home_mode() {
  lamp::apply::brightnessImmediate(73, false, 180);
  TEST_ASSERT_EQUAL_UINT8(73, lamp::mock_config.lamp.brightness);
  TEST_ASSERT_EQUAL_UINT8(0,  lamp::mock_config.homeMode.brightness);
}

void test_immediate_updates_home_brightness_when_home_mode() {
  lamp::apply::brightnessImmediate(40, true, 180);
  TEST_ASSERT_EQUAL_UINT8(40, lamp::mock_config.homeMode.brightness);
  TEST_ASSERT_EQUAL_UINT8(0,  lamp::mock_config.lamp.brightness);
}

void test_immediate_clears_fade_seed() {
  // Pretend a slider drag seeded the fade triple just before.
  lamp::s_userBrightnessSeeded_mock = true;

  lamp::apply::brightnessImmediate(20, false, 180);

  TEST_ASSERT_FALSE(lamp::s_userBrightnessSeeded_mock);
}

void test_immediate_calls_applyEffectiveBrightness() {
  lamp::apply::brightnessImmediate(50, false, 180);
  TEST_ASSERT_EQUAL_INT(1, lamp::effective_brightness_calls);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_immediate_updates_lamp_brightness_when_not_home_mode);
  RUN_TEST(test_immediate_updates_home_brightness_when_home_mode);
  RUN_TEST(test_immediate_clears_fade_seed);
  RUN_TEST(test_immediate_calls_applyEffectiveBrightness);
  return UNITY_END();
}
