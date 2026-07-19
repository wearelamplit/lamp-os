// Native-host pin for PulseExpression's eased travel + loop modes. The wave
// math needs Arduino + a frame buffer, so mirror updateWavePosition() and the
// draw() end-check inline with a mock clock (test_glitchy_timing convention).

#include <unity.h>

#include <algorithm>
#include <cstdint>

#include "util/easing.hpp"

namespace test {

// Mirrors pulse_expression.hpp defaults.
constexpr float kInset = 0.0f;
constexpr uint32_t kEbbMs = 800;

struct PulseTravelRig {
  float progress = 0.0f;
  float travelStart = 0.0f;
  float travelSpan = 1.0f;
  float wavePosition = 0.0f;
  int waveDirection = 1;
  uint32_t pulseSpeedMs = 100;
  lamp::Easing easing = lamp::Easing::Linear;
  bool loop = false;
  bool autoTrigger = true;  // false = transient preview (Test button)
  uint32_t lastUpdateMs = 0;
  uint32_t ebbStartMs = 0;  // set once at first appearance, never by step()
  bool ended = false;
  bool reachedFarEnd = false;

  void trigger(float posMin, float posMax, float pulseWidth) {
    if (loop) {
      travelStart = posMin + kInset;
      travelSpan = std::max(1.0f, (posMax - kInset) - travelStart);
    } else {
      travelStart = posMin - pulseWidth;
      travelSpan = (posMax + 2.0f * pulseWidth) - travelStart;
    }
    progress = 0.0f;
    waveDirection = 1;
    reachedFarEnd = false;
    wavePosition = travelStart;
    lastUpdateMs = 0;
    ended = false;
  }

  float ebb(uint32_t nowMs) const {
    if (!loop) return 1.0f;
    const uint32_t elapsed = nowMs - ebbStartMs;
    if (elapsed >= kEbbMs) return 1.0f;
    return static_cast<float>(elapsed) / static_cast<float>(kEbbMs);
  }

  void step(uint32_t nowMs) {
    if (lastUpdateMs == 0) { lastUpdateMs = nowMs; return; }
    const uint32_t delta = std::min(nowMs - lastUpdateMs, (uint32_t)100);
    lastUpdateMs = nowMs;
    const float dt = static_cast<float>(delta) /
                     (static_cast<float>(pulseSpeedMs) * travelSpan);
    progress += dt * static_cast<float>(waveDirection);
    if (progress >= 1.0f) {
      progress = 1.0f;
      if (loop) { waveDirection = -1; reachedFarEnd = true; }
    } else if (progress <= 0.0f) {
      progress = 0.0f;
      if (loop) waveDirection = 1;
    }
    wavePosition = travelStart + lamp::applyEasing(easing, progress) * travelSpan;
    const bool triggerExit = !loop && progress >= 1.0f;
    const bool previewCycleDone = loop && !autoTrigger && reachedFarEnd && progress <= 0.0f;
    if (triggerExit || previewCycleDone) ended = true;
  }
};

// Old constant-velocity model: wavePosition advances by delta/pulseSpeedMs
// pixels per step, same 100 ms cap.
struct LegacyRig {
  float wavePosition;
  uint32_t pulseSpeedMs;
  uint32_t lastUpdateMs = 0;

  void trigger(float posMin, float pulseWidth) {
    wavePosition = posMin - pulseWidth;
    lastUpdateMs = 0;
  }
  void step(uint32_t nowMs) {
    if (lastUpdateMs == 0) { lastUpdateMs = nowMs; return; }
    const uint32_t delta = std::min(nowMs - lastUpdateMs, (uint32_t)100);
    lastUpdateMs = nowMs;
    wavePosition += static_cast<float>(delta) / static_cast<float>(pulseSpeedMs);
  }
};

}  // namespace test

void setUp(void) {}
void tearDown(void) {}

void test_linear_travel_matches_legacy() {
  test::PulseTravelRig eased;
  eased.easing = lamp::Easing::Linear;
  eased.pulseSpeedMs = 50;
  eased.trigger(/*posMin=*/10, /*posMax=*/40, /*pulseWidth=*/5);

  test::LegacyRig legacy;
  legacy.pulseSpeedMs = 50;
  legacy.trigger(/*posMin=*/10, /*pulseWidth=*/5);

  const uint32_t deltas[] = {16, 16, 200, 33, 16, 16, 80, 16};
  uint32_t nowMs = 1000;
  for (uint32_t d : deltas) {
    nowMs += d;
    eased.step(nowMs);
    legacy.step(nowMs);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, legacy.wavePosition, eased.wavePosition);
  }
}

void test_trigger_mode_ends_on_exit() {
  test::PulseTravelRig rig;
  rig.easing = lamp::Easing::Linear;
  rig.loop = false;
  rig.pulseSpeedMs = 10;
  rig.trigger(0, 20, 3);  // travelSpan = 26 px

  uint32_t nowMs = 1000;
  int guard = 0;
  while (!rig.ended && guard++ < 100000) {
    nowMs += 16;
    rig.step(nowMs);
  }
  TEST_ASSERT_TRUE(rig.ended);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, rig.progress);
}

void test_continuous_reverses_and_never_ends() {
  test::PulseTravelRig rig;
  rig.easing = lamp::Easing::Float;
  rig.loop = true;
  rig.pulseSpeedMs = 10;
  rig.trigger(0, 20, 3);

  bool sawReverseDown = false;  // flipped to -1 at top
  bool sawReverseUp = false;    // flipped back to +1 at bottom
  int prevDir = rig.waveDirection;

  uint32_t nowMs = 1000;
  for (int i = 0; i < 20000; ++i) {
    nowMs += 16;
    rig.step(nowMs);
    if (prevDir == 1 && rig.waveDirection == -1) sawReverseDown = true;
    if (prevDir == -1 && rig.waveDirection == 1) sawReverseUp = true;
    prevDir = rig.waveDirection;
    TEST_ASSERT_FALSE(rig.ended);
  }
  TEST_ASSERT_TRUE(sawReverseDown);
  TEST_ASSERT_TRUE(sawReverseUp);
}

void test_continuous_transient_ends_after_one_cycle() {
  test::PulseTravelRig rig;
  rig.easing = lamp::Easing::Float;
  rig.loop = true;
  rig.autoTrigger = false;  // preview / Test button
  rig.pulseSpeedMs = 10;
  rig.trigger(0, 20, 3);

  uint32_t nowMs = 1000;
  nowMs += 16;
  rig.step(nowMs);  // first step only seeds lastUpdateMs
  TEST_ASSERT_FALSE(rig.ended);

  bool reachedFar = false;
  int guard = 0;
  while (!rig.ended && guard++ < 100000) {
    nowMs += 16;
    rig.step(nowMs);
    if (rig.reachedFarEnd) reachedFar = true;
    if (!reachedFar) TEST_ASSERT_FALSE(rig.ended);              // no stop before far end
    else if (rig.progress > 0.001f) TEST_ASSERT_FALSE(rig.ended);  // no stop mid return
  }
  TEST_ASSERT_TRUE(rig.ended);
  TEST_ASSERT_TRUE(rig.reachedFarEnd);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, rig.progress);  // stopped back at the start end
}

void test_continuous_live_never_ends() {
  test::PulseTravelRig rig;
  rig.easing = lamp::Easing::Float;
  rig.loop = true;
  rig.autoTrigger = true;  // saved / live
  rig.pulseSpeedMs = 10;
  rig.trigger(0, 20, 3);

  uint32_t nowMs = 1000;
  for (int i = 0; i < 20000; ++i) {
    nowMs += 16;
    rig.step(nowMs);
    TEST_ASSERT_FALSE(rig.ended);
  }
}

void test_continuous_travel_spans_visible_edges() {
  test::PulseTravelRig rig;
  rig.easing = lamp::Easing::Float;
  rig.loop = true;
  rig.trigger(/*posMin=*/10, /*posMax=*/40, /*pulseWidth=*/5);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.0f, rig.travelStart);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 40.0f, rig.travelStart + rig.travelSpan);

  // Center reaches the visible edges at the progress extremes, not off-screen.
  rig.progress = 0.0f;
  rig.wavePosition = rig.travelStart + lamp::applyEasing(rig.easing, rig.progress) * rig.travelSpan;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.0f, rig.wavePosition);
  rig.progress = 1.0f;
  rig.wavePosition = rig.travelStart + lamp::applyEasing(rig.easing, rig.progress) * rig.travelSpan;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 40.0f, rig.wavePosition);
}

void test_continuous_ebb_in_ramps_from_zero() {
  test::PulseTravelRig rig;
  rig.loop = true;
  rig.ebbStartMs = 1000;

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, rig.ebb(1000));
  const float mid = rig.ebb(1000 + test::kEbbMs / 2);
  TEST_ASSERT_TRUE(mid > 0.4f && mid < 0.6f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, rig.ebb(1000 + test::kEbbMs));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, rig.ebb(1000 + test::kEbbMs * 10));
}

void test_ebb_does_not_retrigger_on_reversal() {
  test::PulseTravelRig rig;
  rig.easing = lamp::Easing::Float;
  rig.loop = true;
  rig.pulseSpeedMs = 10;
  rig.ebbStartMs = 1000;
  rig.trigger(0, 20, 3);

  uint32_t nowMs = 1000;
  int prevDir = rig.waveDirection;
  bool reversed = false;
  for (int i = 0; i < 20000 && !reversed; ++i) {
    nowMs += 16;
    rig.step(nowMs);
    if (rig.waveDirection != prevDir) reversed = true;
    prevDir = rig.waveDirection;
  }
  TEST_ASSERT_TRUE(reversed);
  // step() never touches ebbStartMs, so the ramp stays a pure function of
  // elapsed time across the turnaround, never resetting to 0.
  const float expected = static_cast<float>(nowMs - 1000) / static_cast<float>(test::kEbbMs);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, std::min(1.0f, expected), rig.ebb(nowMs));
  TEST_ASSERT_TRUE(rig.ebb(nowMs) > 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, rig.ebb(1000 + test::kEbbMs + 100));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_linear_travel_matches_legacy);
  RUN_TEST(test_trigger_mode_ends_on_exit);
  RUN_TEST(test_continuous_reverses_and_never_ends);
  RUN_TEST(test_continuous_transient_ends_after_one_cycle);
  RUN_TEST(test_continuous_live_never_ends);
  RUN_TEST(test_continuous_travel_spans_visible_edges);
  RUN_TEST(test_continuous_ebb_in_ramps_from_zero);
  RUN_TEST(test_ebb_does_not_retrigger_on_reversal);
  return UNITY_END();
}
