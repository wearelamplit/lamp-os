// Native tests for the pin-reading input drivers buildInputs returns.

#include <unity.h>

#include <memory>
#include <vector>

#include "../../src/components/input/button.cpp"
#include "../../src/components/input/input_driver.cpp"
#include "../../src/components/input/touch.cpp"

using lamp::InputSpec;
using lamp::InputType;
using lamp::TouchEvent;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint16_t kIdleRaw = 1000;
constexpr uint16_t kPressedRaw = 900;

std::vector<TouchEvent> g_events;

std::unique_ptr<lamp::input::InputSource> touchAt(uint32_t createdMs) {
  set_mock_millis(createdMs);
  InputSpec spec{};
  spec.id = 1;
  spec.type = InputType::Touch;
  spec.pin = 4;
  auto inputs = lamp::input::buildInputs({spec});
  g_events.clear();
  inputs[0]->asTouch()->setCallback([](TouchEvent e) { g_events.push_back(e); });
  return std::move(inputs[0]);
}

void tickAt(lamp::input::InputSource& in, uint32_t nowMs, uint16_t raw) {
  g_mock_touch_raw = raw;
  in.tick(nowMs);
}

}  // namespace

// Built at boot, calibrated, then idle for a whole wrap: a tap just after the
// wrap lands, where the boot warmup would otherwise swallow it.
void test_touch_warmup_does_not_return_a_wrap_later() {
  const uint32_t createdMs = 1000;
  auto touch = touchAt(createdMs);
  uint32_t t = createdMs + 500;
  while (!touch->asTouch()->isCalibrated()) {
    tickAt(*touch, t, kIdleRaw);
    t += 40;
  }
  for (uint64_t at = t; at < createdMs + (1ull << 32); at += 1ull << 28) {
    tickAt(*touch, static_cast<uint32_t>(at), kIdleRaw);
  }
  tickAt(*touch, createdMs + 100, kPressedRaw);
  tickAt(*touch, createdMs + 200, kIdleRaw);
  TEST_ASSERT_EQUAL_size_t(1, g_events.size());
  TEST_ASSERT_EQUAL(TouchEvent::Tap, g_events[0]);
}

// Inside the warmup no sample is read.
void test_touch_reads_nothing_during_warmup() {
  const uint32_t createdMs = 1000;
  auto touch = touchAt(createdMs);
  for (uint32_t t = createdMs; t < createdMs + 500; t += 40) tickAt(*touch, t, kIdleRaw);
  TEST_ASSERT_FALSE(touch->asTouch()->isCalibrated());
  TEST_ASSERT_EQUAL_UINT16(0, touch->asTouch()->baseline());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_touch_warmup_does_not_return_a_wrap_later);
  RUN_TEST(test_touch_reads_nothing_during_warmup);
  return UNITY_END();
}
