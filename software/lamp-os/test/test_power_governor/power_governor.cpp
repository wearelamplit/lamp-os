// Native tests for the PowerGovernor state machine, same-frame clamp, and
// release glide. Supply 2000 mA throughout: quiet pixel budget 1800,
// radio-hot/boot 1600. Frames run at 16 ms with pixelCount 0 and requested
// 255, so demand equals the fullDutySum passed in.

#include <unity.h>

#include <cstdint>

#include "../../src/core/power_governor.cpp"

using lamp::PowerGovernor;
using State = lamp::PowerGovernor::State;

void setUp() {}
void tearDown() {}

// One production loop iteration every 16 ms: senseFrame then tick.
// Returns true if any frame reported a ceiling snap.
static bool frames(PowerGovernor& gov, uint32_t& now, uint32_t ms,
                   float fullDutySum, uint8_t level = 255,
                   bool radio = false) {
  bool clamped = false;
  for (const uint32_t end = now + ms; now + 16 <= end;) {
    now += 16;
    if (gov.senseFrame(now, fullDutySum, level, 0, radio)) clamped = true;
    gov.tick(now);
  }
  return clamped;
}

// Steps the glide at 50 ms until `ms` elapse, returning the settled ceiling.
static uint8_t converge(PowerGovernor& gov, uint32_t& now, uint32_t ms = 3000) {
  uint8_t c = gov.ceiling(now);
  for (const uint32_t end = now + ms; now + 50 <= end;) {
    now += 50;
    c = gov.ceiling(now);
  }
  return c;
}

// begin at t=0, settled under budget past the 10 s boot window.
static PowerGovernor dormantGov(uint32_t& now) {
  PowerGovernor gov;
  gov.begin(2000, 0);
  now = 10000;
  frames(gov, now, 2000, 100.0f);
  return gov;
}

void test_begin_seeds_boot_ramp_at_half_ceiling() {
  PowerGovernor gov;
  gov.begin(2000, 1000);
  TEST_ASSERT_EQUAL(State::BootRamp, gov.state());
  TEST_ASSERT_EQUAL_UINT8(128, gov.ceiling(1000));
  TEST_ASSERT_TRUE(gov.consumePendingApply());
  TEST_ASSERT_FALSE(gov.consumePendingApply());
}

void test_boot_hold_then_glide_then_dormant() {
  PowerGovernor gov;
  gov.begin(2000, 0);
  uint32_t now = 0;
  frames(gov, now, 4900, 100.0f);
  TEST_ASSERT_EQUAL(State::BootRamp, gov.state());
  TEST_ASSERT_EQUAL_UINT8(128, gov.ceiling(now));
  // Ramp targets move at the 1 s pace; the 400 ms EMA trails by a few units.
  frames(gov, now, 2000, 100.0f);
  TEST_ASSERT_UINT8_WITHIN(8, 154, gov.ceiling(now));
  frames(gov, now, 2000, 100.0f);
  TEST_ASSERT_EQUAL(State::BootRamp, gov.state());
  TEST_ASSERT_UINT8_WITHIN(8, 205, gov.ceiling(now));
  frames(gov, now, 3600, 100.0f);
  TEST_ASSERT_EQUAL(State::Dormant, gov.state());
  TEST_ASSERT_EQUAL_UINT8(255, gov.ceiling(now));
}

void test_boot_window_uses_radio_hot_reserve() {
  PowerGovernor gov;
  gov.begin(2000, 0);
  gov.senseFrame(1000, 100.0f, 255, 0, false);
  TEST_ASSERT_EQUAL_UINT16(1600, gov.pixelBudgetMa());
}

void test_dormant_ceiling_always_255() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  frames(gov, now, 5000, 1700.0f);
  TEST_ASSERT_EQUAL(State::Dormant, gov.state());
  TEST_ASSERT_EQUAL_UINT8(255, gov.ceiling(now));
  gov.consumePendingApply();
  frames(gov, now, 100, 1700.0f);
  TEST_ASSERT_FALSE(gov.consumePendingApply());
}

void test_mild_exceedance_clamps_same_frame() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  gov.consumePendingApply();
  now += 16;
  // 105 % of the quiet budget.
  TEST_ASSERT_TRUE(gov.senseFrame(now, 1890.0f, 255, 0, false));
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
  // fit solves 1890 × (fit+1)/256 == 1800, floored: no glide, same frame.
  TEST_ASSERT_EQUAL_UINT8(242, gov.ceiling(now));
  TEST_ASSERT_TRUE(lamp::demandMa(1890.0f, 242, 0) <= 1800.0f);
  TEST_ASSERT_TRUE(gov.consumePendingApply());
}

void test_gross_exceedance_clamps_same_frame() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  gov.consumePendingApply();
  now += 16;
  TEST_ASSERT_TRUE(gov.senseFrame(now, 3600.0f, 255, 0, false));
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
  // fit solves 3600 × (fit+1)/256 == 1800.
  TEST_ASSERT_EQUAL_UINT8(127, gov.ceiling(now));
  TEST_ASSERT_TRUE(gov.consumePendingApply());
}

void test_clamp_fires_once_at_steady_demand() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  now += 16;
  TEST_ASSERT_TRUE(gov.senseFrame(now, 3600.0f, 255, 0, false));
  gov.consumePendingApply();
  // Clamped ceiling brings the applied level back to budget: no re-fire,
  // no ceiling churn.
  TEST_ASSERT_FALSE(frames(gov, now, 1000, 3600.0f));
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
  TEST_ASSERT_EQUAL_UINT8(127, gov.ceiling(now));
  TEST_ASSERT_FALSE(gov.consumePendingApply());
}

void test_oscillating_demand_does_not_flicker() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  now += 16;
  TEST_ASSERT_TRUE(gov.senseFrame(now, 1890.0f, 255, 0, false));
  TEST_ASSERT_EQUAL_UINT8(242, gov.ceiling(now));
  for (int i = 0; i < 200; ++i) {
    now += 16;
    const float sum = (i & 1) ? 1700.0f : 1890.0f;
    TEST_ASSERT_FALSE(gov.senseFrame(now, sum, 255, 0, false));
    gov.tick(now);
    TEST_ASSERT_EQUAL(State::Clamped, gov.state());
    TEST_ASSERT_EQUAL_UINT8(242, gov.ceiling(now));
  }
  // Deeper exceedance moves the ceiling down immediately.
  now += 16;
  TEST_ASSERT_TRUE(gov.senseFrame(now, 2400.0f, 255, 0, false));
  TEST_ASSERT_EQUAL_UINT8(191, gov.ceiling(now));
  // Easing back over the line does not raise it.
  now += 16;
  TEST_ASSERT_FALSE(gov.senseFrame(now, 1890.0f, 255, 0, false));
  now += 1000;
  gov.tick(now);
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
  TEST_ASSERT_EQUAL_UINT8(191, gov.ceiling(now));
}

void test_release_at_88_percent_of_budget() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  frames(gov, now, 100, 2400.0f);
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
  // Under budget but above 0.88 × 1800 = 1584: stays clamped, ceiling held.
  frames(gov, now, 2500, 1600.0f);
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
  TEST_ASSERT_EQUAL_UINT8(191, gov.ceiling(now));
  frames(gov, now, 1500, 1500.0f);
  TEST_ASSERT_EQUAL(State::Dormant, gov.state());
  TEST_ASSERT_EQUAL_UINT8(255, converge(gov, now));
}

void test_release_paced_by_tick_not_sense() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  frames(gov, now, 100, 2400.0f);
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
  for (int i = 0; i < 200; ++i) {
    now += 16;
    gov.senseFrame(now, 1500.0f, 255, 0, false);
  }
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
  gov.tick(now);
  TEST_ASSERT_EQUAL(State::Dormant, gov.state());
}

void test_reserve_state_flips_borderline_clamp() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  // 1700 is under the quiet budget (1800): never clamps.
  frames(gov, now, 3000, 1700.0f);
  TEST_ASSERT_EQUAL(State::Dormant, gov.state());
  TEST_ASSERT_EQUAL_UINT16(1800, gov.pixelBudgetMa());
  // Same demand over the radio-hot budget (1600): clamps same frame.
  now += 16;
  TEST_ASSERT_TRUE(gov.senseFrame(now, 1700.0f, 255, 0, true));
  TEST_ASSERT_EQUAL_UINT16(1600, gov.pixelBudgetMa());
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
}

void test_boot_over_budget_clamps_below_hold() {
  PowerGovernor gov;
  gov.begin(2000, 0);
  uint32_t now = 16;
  TEST_ASSERT_TRUE(gov.senseFrame(now, 3600.0f, 255, 0, false));
  TEST_ASSERT_EQUAL(State::Clamped, gov.state());
  // fit solves 3600 × (fit+1)/256 == 1600, floored: below the 128 hold.
  TEST_ASSERT_EQUAL_UINT8(112, gov.ceiling(now));
  // Release inside the boot window resumes the ramp.
  frames(gov, now, 1100, 100.0f);
  TEST_ASSERT_EQUAL(State::BootRamp, gov.state());
}

void test_release_glide_monotonic_with_400ms_time_constant() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  now += 16;
  TEST_ASSERT_TRUE(gov.senseFrame(now, 2500.0f, 255, 0, false));
  // fit solves 2500 × (fit+1)/256 == 1800, floored.
  TEST_ASSERT_EQUAL_UINT8(183, gov.ceiling(now));
  now += 16;
  gov.senseFrame(now, 100.0f, 255, 0, false);
  now += 1000;
  gov.tick(now);
  TEST_ASSERT_EQUAL(State::Dormant, gov.state());
  uint8_t prev = gov.ceiling(now);
  TEST_ASSERT_EQUAL_UINT8(183, prev);
  const uint32_t glideStart = now;
  uint8_t at400 = 0;
  while (now < glideStart + 2500) {
    now += 50;
    const uint8_t c = gov.ceiling(now);
    TEST_ASSERT_TRUE(c >= prev);
    prev = c;
    if (now == glideStart + 400) at400 = c;
  }
  // After one time constant ~34% of the 72-step delta remains (50 ms steps).
  TEST_ASSERT_UINT8_WITHIN(10, 230, at400);
  TEST_ASSERT_EQUAL_UINT8(255, prev);
}

void test_pump_self_sustains_while_gliding() {
  uint32_t now = 0;
  PowerGovernor gov = dormantGov(now);
  gov.consumePendingApply();
  now += 16;
  TEST_ASSERT_TRUE(gov.senseFrame(now, 2400.0f, 255, 0, false));
  TEST_ASSERT_TRUE(gov.consumePendingApply());
  TEST_ASSERT_FALSE(gov.consumePendingApply());
  now += 16;
  gov.senseFrame(now, 100.0f, 255, 0, false);
  now += 1000;
  gov.tick(now);
  TEST_ASSERT_EQUAL(State::Dormant, gov.state());
  TEST_ASSERT_TRUE(gov.consumePendingApply());
  for (int i = 0; i < 3; ++i) {
    now += 50;
    gov.ceiling(now);
    TEST_ASSERT_TRUE(gov.consumePendingApply());
  }
  converge(gov, now);
  gov.consumePendingApply();
  now += 50;
  gov.ceiling(now);
  TEST_ASSERT_FALSE(gov.consumePendingApply());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_begin_seeds_boot_ramp_at_half_ceiling);
  RUN_TEST(test_boot_hold_then_glide_then_dormant);
  RUN_TEST(test_boot_window_uses_radio_hot_reserve);
  RUN_TEST(test_dormant_ceiling_always_255);
  RUN_TEST(test_mild_exceedance_clamps_same_frame);
  RUN_TEST(test_gross_exceedance_clamps_same_frame);
  RUN_TEST(test_clamp_fires_once_at_steady_demand);
  RUN_TEST(test_oscillating_demand_does_not_flicker);
  RUN_TEST(test_release_at_88_percent_of_budget);
  RUN_TEST(test_release_paced_by_tick_not_sense);
  RUN_TEST(test_reserve_state_flips_borderline_clamp);
  RUN_TEST(test_boot_over_budget_clamps_below_hold);
  RUN_TEST(test_release_glide_monotonic_with_400ms_time_constant);
  RUN_TEST(test_pump_self_sustains_while_gliding);
  return UNITY_END();
}
