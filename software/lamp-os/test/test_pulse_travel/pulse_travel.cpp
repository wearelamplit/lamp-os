// Native-host pin for PulseExpression's eased travel + loop modes. The wave
// math needs Arduino + a frame buffer, so mirror updateWavePosition() and the
// draw() end-check inline with a mock clock (test_glitchy_timing convention).

#include <unity.h>

#include <algorithm>
#include <cstdint>

#include "expressions/primitives.hpp"
#include "util/easing.hpp"

namespace test {

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
  bool ended = false;
  bool reachedFarEnd = false;
  bool firstEntranceDone = false;
  uint8_t previewCyclesDone = 0;
  float posMin = 0.0f;
  float posMax = 0.0f;

  void trigger(float inMin, float inMax, float pulseWidth) {
    posMin = inMin;
    posMax = inMax;
    if (loop) {
      travelStart = inMin - pulseWidth;
      travelSpan = std::max(1.0f, inMax - travelStart);
    } else {
      travelStart = inMin - pulseWidth;
      travelSpan = (inMax + 2.0f * pulseWidth) - travelStart;
    }
    progress = 0.0f;
    waveDirection = 1;
    reachedFarEnd = false;
    firstEntranceDone = false;
    wavePosition = travelStart;
    lastUpdateMs = 0;
    ended = false;
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
      if (loop) {
        if (!firstEntranceDone) {
          firstEntranceDone = true;
          travelStart = posMin;
          travelSpan = std::max(1.0f, posMax - travelStart);
        }
        waveDirection = -1;
        reachedFarEnd = true;
      }
    } else if (progress <= 0.0f) {
      progress = 0.0f;
      if (loop) waveDirection = 1;
    }
    wavePosition = travelStart + lamp::applyEasing(easing, progress) * travelSpan;
    const bool triggerExit = !loop && progress >= 1.0f;
    const bool cycleReturned = loop && reachedFarEnd && progress <= 0.0f;
    bool previewCycleDone = false;
    if (cycleReturned && !autoTrigger) {
      previewCycleDone = ++previewCyclesDone >= lamp::kPreviewCycles;
    }
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

// Mirrors PulseExpression::control() dispatch over Expression::control() and
// continuousControl(). STOPPED/PLAYING mirror AnimationState (Arduino-free).
struct ControlRig {
  static constexpr int STOPPED = 5;
  static constexpr int PLAYING = 1;

  bool loopContinuous = false;
  bool autoTriggerEnabled = true;
  int state = STOPPED;
  uint32_t nextTriggerMs = 0;
  bool triggered = false;
  uint32_t currentLoop = 0;
  uint32_t lastCompletedLoop = 0;
  int onCompleteCalls = 0;

  void doTrigger() { triggered = true; state = PLAYING; }

  bool isAnimationComplete() const {
    return state == STOPPED && lastCompletedLoop > 0;
  }

  void control(uint32_t nowMs) {
    if (!loopContinuous) {
      if (autoTriggerEnabled && state == STOPPED &&
          static_cast<int32_t>(nowMs - nextTriggerMs) >= 0) {
        doTrigger();
      }
      return;
    }
    if (autoTriggerEnabled && state == STOPPED) doTrigger();
    // Mirror PulseExpression::control()'s onComplete dispatch.
    if (state == STOPPED && currentLoop > lastCompletedLoop) {
      ++onCompleteCalls;
      lastCompletedLoop = currentLoop;
    }
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

void test_continuous_first_entrance_starts_off_strip() {
  test::PulseTravelRig rig;
  rig.easing = lamp::Easing::Float;
  rig.loop = true;
  rig.trigger(/*posMin=*/10, /*posMax=*/40, /*pulseWidth=*/5);

  // Leg 0 spawns fully off-strip (center a pulse-width below the near edge)
  // and sweeps to the far edge, no in-view pop-on.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, rig.travelStart);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, rig.wavePosition);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 40.0f, rig.travelStart + rig.travelSpan);
}

void test_continuous_repoints_to_visible_edges_after_entrance() {
  test::PulseTravelRig rig;
  rig.easing = lamp::Easing::Float;
  rig.loop = true;
  rig.pulseSpeedMs = 10;
  rig.trigger(/*posMin=*/10, /*posMax=*/40, /*pulseWidth=*/5);

  uint32_t nowMs = 1000;
  int guard = 0;
  while (!rig.firstEntranceDone && guard++ < 100000) {
    nowMs += 16;
    rig.step(nowMs);
  }
  TEST_ASSERT_TRUE(rig.firstEntranceDone);
  // Ongoing ping-pong bunches at each visible edge: center range [posMin, posMax].
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.0f, rig.travelStart);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 40.0f, rig.travelStart + rig.travelSpan);
  // Landed the entrance at the far edge, no jump-back.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 40.0f, rig.wavePosition);

  // Ping-pong turnaround pixels: center reaches exactly posMax at progress 1
  // and posMin at progress 0 (bunch, not exit).
  rig.progress = 0.0f;
  rig.wavePosition = rig.travelStart + lamp::applyEasing(rig.easing, rig.progress) * rig.travelSpan;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.0f, rig.wavePosition);
  rig.progress = 1.0f;
  rig.wavePosition = rig.travelStart + lamp::applyEasing(rig.easing, rig.progress) * rig.travelSpan;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 40.0f, rig.wavePosition);
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

void test_continuous_pingpong_stays_within_visible_edges() {
  test::PulseTravelRig rig;
  rig.easing = lamp::Easing::Float;
  rig.loop = true;
  rig.pulseSpeedMs = 10;
  rig.trigger(/*posMin=*/10, /*posMax=*/40, /*pulseWidth=*/5);

  uint32_t nowMs = 1000;
  // Run past the entrance leg, then confirm the center never exceeds the
  // visible edges (bunch at the edge, never a full exit) across many bounces.
  int guard = 0;
  while (!rig.firstEntranceDone && guard++ < 100000) {
    nowMs += 16;
    rig.step(nowMs);
  }
  for (int i = 0; i < 20000; ++i) {
    nowMs += 16;
    rig.step(nowMs);
    TEST_ASSERT_TRUE(rig.wavePosition >= 10.0f - 1e-3f);
    TEST_ASSERT_TRUE(rig.wavePosition <= 40.0f + 1e-3f);
  }
}

void test_continuous_transient_ends_after_two_cycles() {
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

  int guard = 0;
  while (!rig.ended && guard++ < 200000) {
    nowMs += 16;
    rig.step(nowMs);
    // The preview must loop, not stop on the first return to the near edge.
    if (rig.previewCyclesDone < lamp::kPreviewCycles) TEST_ASSERT_FALSE(rig.ended);
  }
  TEST_ASSERT_TRUE(rig.ended);
  TEST_ASSERT_EQUAL_UINT8(lamp::kPreviewCycles, rig.previewCyclesDone);
  TEST_ASSERT_TRUE(rig.reachedFarEnd);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, rig.progress);  // stopped back at the near edge
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

void test_continuous_pulse_triggers_promptly() {
  test::ControlRig rig;
  rig.loopContinuous = true;
  rig.autoTriggerEnabled = true;
  rig.nextTriggerMs = 1000000;  // far future; a saved Pulse must not wait for it
  rig.control(0);
  TEST_ASSERT_TRUE(rig.triggered);
}

void test_continuous_pulse_self_heals_from_stopped() {
  test::ControlRig rig;
  rig.loopContinuous = true;
  rig.autoTriggerEnabled = true;
  rig.state = test::ControlRig::STOPPED;
  rig.nextTriggerMs = 1000000;
  rig.control(500);
  TEST_ASSERT_TRUE(rig.triggered);
}

void test_trigger_mode_pulse_waits_for_interval() {
  test::ControlRig rig;
  rig.loopContinuous = false;
  rig.autoTriggerEnabled = true;
  rig.nextTriggerMs = 1000;
  rig.control(0);
  TEST_ASSERT_FALSE(rig.triggered);  // before the interval, no start
  rig.control(1000);
  TEST_ASSERT_TRUE(rig.triggered);   // at the interval, scheduled start
}

void test_continuous_pulse_preview_does_not_autotrigger() {
  test::ControlRig rig;
  rig.loopContinuous = true;
  rig.autoTriggerEnabled = false;  // transient preview; manager triggers it
  rig.control(0);
  TEST_ASSERT_FALSE(rig.triggered);
}

void test_continuous_pulse_finished_preview_is_reap_eligible() {
  // A finished continuous preview: draw() set frames=frame+1, nextFrame() drove
  // it to STOPPED with currentLoop advanced. The overridden control() must
  // still fire the onComplete dispatch so isAnimationComplete() reads true.
  test::ControlRig rig;
  rig.loopContinuous = true;
  rig.autoTriggerEnabled = false;
  rig.state = test::ControlRig::STOPPED;
  rig.currentLoop = 1;
  rig.lastCompletedLoop = 0;

  TEST_ASSERT_FALSE(rig.isAnimationComplete());
  rig.control(0);
  TEST_ASSERT_FALSE(rig.triggered);  // transient never auto-retriggers
  TEST_ASSERT_EQUAL_INT(1, rig.onCompleteCalls);
  TEST_ASSERT_TRUE(rig.isAnimationComplete());
}

void test_size_step_maps_to_width_percent() {
  // 1..10 folds linearly onto 25..100%, step 4 = 50%.
  TEST_ASSERT_EQUAL_UINT16(25, lamp::pulseSizePercentFromStep(1));
  TEST_ASSERT_EQUAL_UINT16(50, lamp::pulseSizePercentFromStep(4));
  TEST_ASSERT_EQUAL_UINT16(100, lamp::pulseSizePercentFromStep(10));

  const uint16_t zone = 100;
  TEST_ASSERT_EQUAL_UINT16(13, lamp::pulseWidthFromPercent(lamp::pulseSizePercentFromStep(1), zone));
  TEST_ASSERT_EQUAL_UINT16(25, lamp::pulseWidthFromPercent(lamp::pulseSizePercentFromStep(4), zone));
  TEST_ASSERT_EQUAL_UINT16(50, lamp::pulseWidthFromPercent(lamp::pulseSizePercentFromStep(10), zone));
}

void test_size_out_of_range_folds_to_default_step() {
  // 0 (absent), 40 (legacy raw percent), 11 (over max) all land on step 4 = 50%.
  const uint16_t step4 = lamp::pulseSizePercentFromStep(4);
  TEST_ASSERT_EQUAL_UINT16(step4, lamp::pulseSizePercentFromStep(0));
  TEST_ASSERT_EQUAL_UINT16(step4, lamp::pulseSizePercentFromStep(40));
  TEST_ASSERT_EQUAL_UINT16(step4, lamp::pulseSizePercentFromStep(11));

  const uint16_t zone = 100;
  const uint16_t w4 = lamp::pulseWidthFromPercent(step4, zone);
  TEST_ASSERT_EQUAL_UINT16(w4, lamp::pulseWidthFromPercent(lamp::pulseSizePercentFromStep(0), zone));
  TEST_ASSERT_EQUAL_UINT16(w4, lamp::pulseWidthFromPercent(lamp::pulseSizePercentFromStep(40), zone));
  TEST_ASSERT_EQUAL_UINT16(w4, lamp::pulseWidthFromPercent(lamp::pulseSizePercentFromStep(11), zone));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_size_step_maps_to_width_percent);
  RUN_TEST(test_size_out_of_range_folds_to_default_step);
  RUN_TEST(test_linear_travel_matches_legacy);
  RUN_TEST(test_trigger_mode_ends_on_exit);
  RUN_TEST(test_continuous_first_entrance_starts_off_strip);
  RUN_TEST(test_continuous_repoints_to_visible_edges_after_entrance);
  RUN_TEST(test_continuous_reverses_and_never_ends);
  RUN_TEST(test_continuous_pingpong_stays_within_visible_edges);
  RUN_TEST(test_continuous_transient_ends_after_two_cycles);
  RUN_TEST(test_continuous_live_never_ends);
  RUN_TEST(test_continuous_pulse_triggers_promptly);
  RUN_TEST(test_continuous_pulse_self_heals_from_stopped);
  RUN_TEST(test_trigger_mode_pulse_waits_for_interval);
  RUN_TEST(test_continuous_pulse_preview_does_not_autotrigger);
  RUN_TEST(test_continuous_pulse_finished_preview_is_reap_eligible);
  return UNITY_END();
}
