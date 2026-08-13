// Native FSM tests for the capacitive-pad gesture layer. touched = LOWER
// raw value than the calibrated baseline. Injected clock + raw samples.

#include <unity.h>

#include <vector>

#include "components/input/touch.hpp"
#include "../../src/components/input/touch.cpp"

using lamp::Touch;
using lamp::TouchEvent;
using lamp::TouchTuning;

static std::vector<TouchEvent> g_events;

static Touch makeTouch() {
  TouchTuning t{};
  t.calibrationSamples = 8;
  t.pressDelta = 12;
  t.releaseDelta = 6;
  t.holdMs = 400;
  Touch pad(t);
  pad.setCallback([](TouchEvent e) { g_events.push_back(e); });
  return pad;
}

// Feed calibration samples around `ambient` so the baseline lands there.
static void calibrate(Touch& pad, uint16_t ambient) {
  for (int i = 0; i < 8; ++i) pad.update(ambient, i * 30);
}

void setUp(void) { g_events.clear(); }
void tearDown(void) {}

void test_calibration_seeds_baseline() {
  Touch pad = makeTouch();
  TEST_ASSERT_FALSE(pad.isCalibrated());
  calibrate(pad, 2000);
  TEST_ASSERT_TRUE(pad.isCalibrated());
  TEST_ASSERT_EQUAL_UINT16(2000, pad.baseline());
  // no events emitted during calibration
  TEST_ASSERT_EQUAL(0, g_events.size());
}

void test_touched_is_lower_value() {
  Touch pad = makeTouch();
  calibrate(pad, 2000);
  // raw ABOVE baseline is never a touch
  pad.update(2050, 1000);
  TEST_ASSERT_FALSE(pad.isHeld());
  TEST_ASSERT_EQUAL(0, g_events.size());
}

void test_tap_then_release() {
  Touch pad = makeTouch();
  calibrate(pad, 2000);
  pad.update(1980, 1000);  // delta 20 > pressDelta -> touched
  pad.update(2000, 1100);  // delta 0 < releaseDelta -> released, short = tap
  TEST_ASSERT_EQUAL(1, g_events.size());
  TEST_ASSERT_EQUAL(static_cast<int>(TouchEvent::Tap),
                    static_cast<int>(g_events[0]));
}

void test_hold_start_then_release() {
  Touch pad = makeTouch();
  calibrate(pad, 2000);
  pad.update(1980, 1000);       // touched
  pad.update(1980, 1450);       // held past holdMs (400) -> HoldStart
  TEST_ASSERT_EQUAL(1, g_events.size());
  TEST_ASSERT_EQUAL(static_cast<int>(TouchEvent::HoldStart),
                    static_cast<int>(g_events[0]));
  TEST_ASSERT_TRUE(pad.isHeld());
  pad.update(2000, 1500);       // release after a hold -> Release
  TEST_ASSERT_EQUAL(2, g_events.size());
  TEST_ASSERT_EQUAL(static_cast<int>(TouchEvent::Release),
                    static_cast<int>(g_events[1]));
  TEST_ASSERT_FALSE(pad.isHeld());
}

void test_hysteresis_holds_through_noise() {
  Touch pad = makeTouch();
  calibrate(pad, 2000);
  pad.update(1980, 1000);   // touched (delta 20)
  // raw rises into the dead band (delta between release 6 and press 12):
  // 1990 -> delta 10, not a release
  pad.update(1990, 1050);
  TEST_ASSERT_EQUAL(0, g_events.size());  // still held, no release yet
  pad.update(2000, 1100);   // delta 0 -> release (tap, no hold fired)
  TEST_ASSERT_EQUAL(1, g_events.size());
  TEST_ASSERT_EQUAL(static_cast<int>(TouchEvent::Tap),
                    static_cast<int>(g_events[0]));
}

void test_untouched_drift_tracks_baseline() {
  Touch pad = makeTouch();
  calibrate(pad, 2000);
  // A small ambient shift that stays inside the press threshold (delta 8 < 12)
  // is drift, not a touch: the baseline slowly tracks toward it via the EMA.
  for (int i = 0; i < 400; ++i) pad.update(1992, 2000 + i * 20);
  TEST_ASSERT_TRUE(pad.baseline() <= 1994);
  TEST_ASSERT_TRUE(pad.baseline() >= 1990);
  // and no touch was ever registered during the drift
  TEST_ASSERT_EQUAL(0, g_events.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_calibration_seeds_baseline);
  RUN_TEST(test_touched_is_lower_value);
  RUN_TEST(test_tap_then_release);
  RUN_TEST(test_hold_start_then_release);
  RUN_TEST(test_hysteresis_holds_through_noise);
  RUN_TEST(test_untouched_drift_tracks_baseline);
  return UNITY_END();
}
