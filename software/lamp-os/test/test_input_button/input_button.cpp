// Native FSM tests for the momentary-button gesture layer. Injected clock;
// update(pressed, nowMs) is driven directly, no Arduino.

#include <unity.h>

#include <vector>

#include "components/input/button.hpp"
#include "../../src/components/input/button.cpp"

using lamp::Button;
using lamp::ButtonEvent;
using lamp::ButtonTiming;

static std::vector<ButtonEvent> g_events;

static Button makeButton() {
  Button b(ButtonTiming{});  // defaults: debounce 25, doubleGap 300, long 600
  b.setCallback([](ButtonEvent e) { g_events.push_back(e); });
  return b;
}

void setUp(void) { g_events.clear(); }
void tearDown(void) {}

// Commit a debounced level change: hold the raw sample across the debounce
// window so update() latches it.
static void hold(Button& b, bool pressed, uint32_t startMs, uint32_t ms) {
  for (uint32_t t = 0; t <= ms; t += 5) b.update(pressed, startMs + t);
}

void test_single_click() {
  Button b = makeButton();
  hold(b, true, 0, 50);      // press
  hold(b, false, 60, 50);    // release ~t=110
  // resolve after doubleGap with no second press
  b.update(false, 500);
  TEST_ASSERT_EQUAL(1, g_events.size());
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonEvent::Click),
                    static_cast<int>(g_events[0]));
}

void test_double_click() {
  Button b = makeButton();
  hold(b, true, 0, 40);
  hold(b, false, 50, 40);    // first release ~t=95
  hold(b, true, 120, 40);    // second press within doubleGap
  hold(b, false, 170, 40);   // second release ~t=215
  b.update(false, 600);      // resolve
  TEST_ASSERT_EQUAL(1, g_events.size());
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonEvent::DoubleClick),
                    static_cast<int>(g_events[0]));
}

void test_long_press_start_and_stop() {
  Button b = makeButton();
  // press and hold past longPressMs (600)
  hold(b, true, 0, 700);
  TEST_ASSERT_EQUAL(1, g_events.size());
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonEvent::LongPressStart),
                    static_cast<int>(g_events[0]));
  TEST_ASSERT_TRUE(b.isHeld());
  TEST_ASSERT_TRUE(b.longActive());
  // release
  hold(b, false, 750, 50);
  TEST_ASSERT_EQUAL(2, g_events.size());
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonEvent::LongPressStop),
                    static_cast<int>(g_events[1]));
  TEST_ASSERT_FALSE(b.isHeld());
}

void test_long_press_does_not_emit_click() {
  Button b = makeButton();
  hold(b, true, 0, 700);
  hold(b, false, 750, 50);
  b.update(false, 2000);  // well past doubleGap
  // exactly LongPressStart + LongPressStop, no Click
  TEST_ASSERT_EQUAL(2, g_events.size());
  for (auto e : g_events) {
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(ButtonEvent::Click),
                          static_cast<int>(e));
  }
}

void test_held_ms_tracks_press() {
  Button b = makeButton();
  hold(b, true, 1000, 40);  // debounced press latched by ~1030
  TEST_ASSERT_TRUE(b.isHeld());
  const uint32_t held = b.heldMs(1200);
  TEST_ASSERT_TRUE(held >= 170 && held <= 200);
}

void test_debounce_rejects_glitch() {
  Button b = makeButton();
  // a single-sample glitch shorter than debounce never latches
  b.update(true, 0);
  b.update(false, 5);
  b.update(false, 500);
  TEST_ASSERT_EQUAL(0, g_events.size());
  TEST_ASSERT_FALSE(b.isHeld());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_click);
  RUN_TEST(test_double_click);
  RUN_TEST(test_long_press_start_and_stop);
  RUN_TEST(test_long_press_does_not_emit_click);
  RUN_TEST(test_held_ms_tracks_press);
  RUN_TEST(test_debounce_rejects_glitch);
  return UNITY_END();
}
