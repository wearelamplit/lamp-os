// Mirror-class test for the brightness fade settle path.
//
// Invariants under test:
//   1. settleBrightnessFade() leaves seeded=true (not cleared).
//   2. settleBrightnessFade() sets source==target.
//   3. After settle, the next setBrightnessFade call sources from the settled
//      level (micro-fade), not the new target (cold snap). This pins the
//      regression: the tick's fade-completion must call settleBrightnessFade,
//      not clearBrightnessFadeSeed.
//
// Mirror discipline: matches production lamp_brightness.cpp state +
// apply_brightness.hpp brightnessToConfig logic. If production changes,
// update the mirror.

#include <unity.h>

#include <cstdint>

namespace lamp {

// Mirror of lamp_brightness.cpp file-statics.
static uint8_t  s_userBrightnessSource    = 0;
static uint8_t  s_userBrightnessTarget    = 0;
static uint32_t s_userBrightnessFadeStart = 0;
static bool     s_userBrightnessSeeded    = false;

static constexpr uint16_t kUserBrightnessFadeMs = 80;

// Mirror of lamp_brightness.cpp computeUserBrightnessNow.
uint8_t computeUserBrightnessNow(uint32_t nowMs) {
  if (s_userBrightnessSource == s_userBrightnessTarget)
    return s_userBrightnessTarget;
  const uint32_t elapsed = nowMs - s_userBrightnessFadeStart;
  if (elapsed >= kUserBrightnessFadeMs)
    return s_userBrightnessTarget;
  const int32_t span =
      static_cast<int32_t>(s_userBrightnessTarget) - s_userBrightnessSource;
  return static_cast<uint8_t>(
      s_userBrightnessSource +
      (span * static_cast<int32_t>(elapsed)) /
          static_cast<int32_t>(kUserBrightnessFadeMs));
}

bool    brightnessFadeSeeded() { return s_userBrightnessSeeded; }
uint8_t brightnessFadeSource() { return s_userBrightnessSource; }
uint8_t brightnessFadeTarget() { return s_userBrightnessTarget; }

void setBrightnessFade(uint8_t source, uint8_t target, uint32_t startMs) {
  s_userBrightnessSource    = source;
  s_userBrightnessTarget    = target;
  s_userBrightnessFadeStart = startMs;
  s_userBrightnessSeeded    = true;
}

void clearBrightnessFadeSeed() { s_userBrightnessSeeded = false; }

// Mirror of lamp_brightness.cpp settleBrightnessFade.
void settleBrightnessFade() { s_userBrightnessSource = s_userBrightnessTarget; }

}  // namespace lamp

void setUp(void) {
  lamp::s_userBrightnessSource    = 0;
  lamp::s_userBrightnessTarget    = 0;
  lamp::s_userBrightnessFadeStart = 0;
  lamp::s_userBrightnessSeeded    = false;
}
void tearDown(void) {}

void test_settle_keeps_seeded_true() {
  // Simulate a fade in progress (80→150).
  lamp::setBrightnessFade(80, 150, 1000);

  // Tick completes: level reached target, tick calls settleBrightnessFade.
  lamp::settleBrightnessFade();

  TEST_ASSERT_TRUE(lamp::brightnessFadeSeeded());
}

void test_settle_sets_source_equal_to_target() {
  lamp::setBrightnessFade(80, 150, 1000);
  lamp::settleBrightnessFade();

  TEST_ASSERT_EQUAL_UINT8(150, lamp::brightnessFadeSource());
  TEST_ASSERT_EQUAL_UINT8(150, lamp::brightnessFadeTarget());
}

void test_next_drag_after_settle_sources_from_settled_level() {
  // Fade 80→150 completes and settles.
  lamp::setBrightnessFade(80, 150, 1000);
  lamp::settleBrightnessFade();

  // Next slider write: app sends 200. brightnessToConfig picks the source
  // from computeUserBrightnessNow() because seeded==true. With
  // source==target==150 post-settle, computeUserBrightnessNow returns 150.
  const uint32_t now = 2000;
  const uint8_t newTarget = 200;
  const uint8_t source = lamp::brightnessFadeSeeded()
                             ? lamp::computeUserBrightnessNow(now)
                             : newTarget;  // cold-snap path
  lamp::setBrightnessFade(source, newTarget, now);

  // Micro-fade: source is settled level (150), not the new target (200).
  TEST_ASSERT_EQUAL_UINT8(150, lamp::brightnessFadeSource());
}

void test_clear_seed_would_cold_snap() {
  // Contrast: if tick had called clearBrightnessFadeSeed instead, the next
  // drag takes the cold-snap path (source == newTarget, no micro-fade).
  lamp::setBrightnessFade(80, 150, 1000);
  lamp::clearBrightnessFadeSeed();  // the regression

  const uint32_t now = 2000;
  const uint8_t newTarget = 200;
  const uint8_t source = lamp::brightnessFadeSeeded()
                             ? lamp::computeUserBrightnessNow(now)
                             : newTarget;
  lamp::setBrightnessFade(source, newTarget, now);

  // Cold snap: source == newTarget, brightness jumps instantly.
  TEST_ASSERT_EQUAL_UINT8(200, lamp::brightnessFadeSource());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_settle_keeps_seeded_true);
  RUN_TEST(test_settle_sets_source_equal_to_target);
  RUN_TEST(test_next_drag_after_settle_sources_from_settled_level);
  RUN_TEST(test_clear_seed_would_cold_snap);
  return UNITY_END();
}
