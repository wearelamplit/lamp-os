#include <unity.h>
#include "../../src/lamps/lioness/lions_greeting.hpp"

using namespace lamp::lioness;

void setUp() {}
void tearDown() {}

// Exactly kPulses brightness peaks across the pulse window. Counts contiguous
// near-peak runs rather than single-frame local maxima: at this frame
// resolution, uint8_t truncation can hold two adjacent frames at the same
// near-255 value, which a naive prev/cur/next compare double-counts.
void test_pulse_peak_count() {
  int peaks = 0;
  bool inPeak = false;
  const uint32_t pulseWindow = kPulses * kFramesPerPulse;
  for (uint32_t f = 0; f < pulseWindow; ++f) {
    const bool nearPeak = pulseEnvelope(f) >= kPulsePeak - 5;
    if (nearPeak && !inPeak) ++peaks;
    inPeak = nearPeak;
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kPulses), peaks);
}

// Troughs between pulses sit at ~kPulseFloor (breathe), not near-off
// (strobe); peaks still reach full brightness.
void test_pulses_breathe_floor() {
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(kPulseFloor, pulseEnvelope(0));
  TEST_ASSERT_LESS_OR_EQUAL_UINT8(kPulseFloor + 15, pulseEnvelope(0));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(kPulseFloor, pulseEnvelope(kFramesPerPulse));
  TEST_ASSERT_LESS_OR_EQUAL_UINT8(kPulseFloor + 15, pulseEnvelope(kFramesPerPulse));
  TEST_ASSERT_EQUAL_UINT8(kPulsePeak, pulseEnvelope(kFramesPerPulse / 2));
}

// No re-brighten flash at the pulse->ease boundary: the tail starts exactly
// where the last pulse leaves off and decays monotonically to 0.
void test_tail_monotonic_no_jump() {
  const uint32_t pulseWindow = kPulses * kFramesPerPulse;
  const uint32_t lastPeakFrame = (kPulses - 1) * kFramesPerPulse + kFramesPerPulse / 2;

  TEST_ASSERT_EQUAL_UINT8(pulseEnvelope(pulseWindow - 1), pulseEnvelope(pulseWindow));

  uint8_t prev = pulseEnvelope(lastPeakFrame);
  for (uint32_t f = lastPeakFrame + 1; f <= kGreetFrames; ++f) {
    const uint8_t cur = pulseEnvelope(f);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(prev, cur);
    prev = cur;
  }
}

// Past the greeting window the envelope is fully off (ambient owns the lions).
void test_after_window_zero() {
  TEST_ASSERT_EQUAL_UINT8(0, pulseEnvelope(kGreetFrames));
  TEST_ASSERT_EQUAL_UINT8(0, pulseEnvelope(kGreetFrames + 100));
}

// Regression guard for the calm-breathe feel: a slow cadence (~2.2-2.6s per
// breath) and a shallow, gentle swing (~64-70% floor), not an excited pulse.
void test_cadence_reads_as_gentle_breathe() {
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2200, kPulseMs);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(2600, kPulseMs);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(163, kPulseFloor);
  TEST_ASSERT_LESS_OR_EQUAL_UINT8(179, kPulseFloor);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pulse_peak_count);
  RUN_TEST(test_pulses_breathe_floor);
  RUN_TEST(test_tail_monotonic_no_jump);
  RUN_TEST(test_after_window_zero);
  RUN_TEST(test_cadence_reads_as_gentle_breathe);
  return UNITY_END();
}
