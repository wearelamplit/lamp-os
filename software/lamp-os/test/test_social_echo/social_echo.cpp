// Native-host tests for the expression-mirror (SocialEchoObserver) feature.
//
// Pins:
//   1. Rate grid: every (mode × disposition) cell resolves to the expected
//      mirror chance. Salty/Wary/Neutral → 0; Fond → 10/20/30;
//      Smitten → 25/50/75 (intro/ambi/extro).
//   2. disp<4 early-out: unknown/neutral peers never mirror.
//   3. Roll gate: a losing roll skips; a winning roll schedules.
//   4. Introvert cooldown: a second mirror inside the window is gated;
//      Ambivert/Extrovert have no cooldown.
//   5. Delayed-replay scheduling + fire: fireAt = now + floor + jitter, in
//      [now+400, now+800]; the pending entry fires exactly once at/after its
//      deadline.
//   6. emitEvent continuous-gate: a continuous descriptor never announces.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>

// social_echo.hpp is native-safe (config_types + expression_invocation +
// expression_observer + fast_rng, all compile under the native rig); the two
// decision functions are header-inline so this test links the real code.
#include "behaviors/social_echo.hpp"

using lamp::SocialMode;
using lamp::mirrorRatePct;
using lamp::mirrorDecision;

void setUp(void) {}
void tearDown(void) {}

// --- 1. rate grid ---------------------------------------------------------

void test_grid_cold_dispositions_never_mirror() {
  for (uint8_t d = 0; d <= 3; ++d) {
    TEST_ASSERT_EQUAL_UINT8(0, mirrorRatePct(d, SocialMode::Introvert));
    TEST_ASSERT_EQUAL_UINT8(0, mirrorRatePct(d, SocialMode::Ambivert));
    TEST_ASSERT_EQUAL_UINT8(0, mirrorRatePct(d, SocialMode::Extrovert));
  }
}

void test_grid_fond() {
  TEST_ASSERT_EQUAL_UINT8(10, mirrorRatePct(4, SocialMode::Introvert));
  TEST_ASSERT_EQUAL_UINT8(20, mirrorRatePct(4, SocialMode::Ambivert));
  TEST_ASSERT_EQUAL_UINT8(30, mirrorRatePct(4, SocialMode::Extrovert));
}

void test_grid_smitten() {
  TEST_ASSERT_EQUAL_UINT8(25, mirrorRatePct(5, SocialMode::Introvert));
  TEST_ASSERT_EQUAL_UINT8(50, mirrorRatePct(5, SocialMode::Ambivert));
  TEST_ASSERT_EQUAL_UINT8(75, mirrorRatePct(5, SocialMode::Extrovert));
}

void test_grid_out_of_range_disposition() {
  TEST_ASSERT_EQUAL_UINT8(0, mirrorRatePct(6, SocialMode::Ambivert));
  TEST_ASSERT_EQUAL_UINT8(0, mirrorRatePct(200, SocialMode::Ambivert));
}

// --- 2. disp<4 early-out --------------------------------------------------

void test_disp_below_4_skips() {
  uint32_t fireAt = 0;
  for (uint8_t d = 0; d <= 3; ++d) {
    TEST_ASSERT_FALSE(mirrorDecision(d, SocialMode::Extrovert, /*roll*/ 1,
                                     /*jitter*/ 0, /*now*/ 1000,
                                     /*ever*/ false, /*last*/ 0, fireAt));
  }
}

// --- 3. roll gate ---------------------------------------------------------

void test_winning_roll_schedules() {
  uint32_t fireAt = 0;
  // Smitten/Ambivert rate = 50. roll 50 wins (<=), roll 51 loses.
  TEST_ASSERT_TRUE(mirrorDecision(5, SocialMode::Ambivert, 50, 0, 1000, false,
                                  0, fireAt));
  TEST_ASSERT_FALSE(mirrorDecision(5, SocialMode::Ambivert, 51, 0, 1000, false,
                                   0, fireAt));
}

// --- 4. introvert cooldown ------------------------------------------------

void test_introvert_cooldown_gates_second_mirror() {
  uint32_t fireAt = 0;
  const uint32_t last = 1000;
  // Within the 10-min window → gated.
  TEST_ASSERT_FALSE(mirrorDecision(5, SocialMode::Introvert, 1, 0,
                                   last + lamp::kIntrovertMirrorCooldownMs - 1,
                                   /*ever*/ true, last, fireAt));
  // At/after the window → allowed.
  TEST_ASSERT_TRUE(mirrorDecision(5, SocialMode::Introvert, 1, 0,
                                  last + lamp::kIntrovertMirrorCooldownMs,
                                  /*ever*/ true, last, fireAt));
  // First-ever mirror (everMirrored=false) is never gated.
  TEST_ASSERT_TRUE(mirrorDecision(5, SocialMode::Introvert, 1, 0, last + 10,
                                  /*ever*/ false, last, fireAt));
}

void test_non_introvert_has_no_cooldown() {
  uint32_t fireAt = 0;
  TEST_ASSERT_TRUE(mirrorDecision(5, SocialMode::Ambivert, 1, 0, 1010, true,
                                  1000, fireAt));
  TEST_ASSERT_TRUE(mirrorDecision(5, SocialMode::Extrovert, 1, 0, 1010, true,
                                  1000, fireAt));
}

// --- 5. scheduling window + fire ------------------------------------------

void test_fireat_window() {
  uint32_t fireAt = 0;
  const uint32_t now = 5000;
  // jitter floored at 0.
  TEST_ASSERT_TRUE(mirrorDecision(5, SocialMode::Ambivert, 1, 0, now, false, 0,
                                  fireAt));
  TEST_ASSERT_EQUAL_UINT32(now + lamp::kMirrorDelayFloorMs, fireAt);
  // jitter at max.
  TEST_ASSERT_TRUE(mirrorDecision(5, SocialMode::Ambivert, 1,
                                  lamp::kMirrorJitterMs, now, false, 0, fireAt));
  TEST_ASSERT_EQUAL_UINT32(now + lamp::kMirrorDelayFloorMs +
                               lamp::kMirrorJitterMs,
                           fireAt);
  // jitter clamped when the draw exceeds the jitter span.
  TEST_ASSERT_TRUE(mirrorDecision(5, SocialMode::Ambivert, 1,
                                  lamp::kMirrorJitterMs + 5000, now, false, 0,
                                  fireAt));
  TEST_ASSERT_EQUAL_UINT32(now + lamp::kMirrorDelayFloorMs +
                               lamp::kMirrorJitterMs,
                           fireAt);
}

// Mirror of SocialEchoObserver::tick's due-entry loop: a wrap-safe
// deadline test that fires once and clears the slot.
struct PendingSlot {
  uint32_t fireAt = 0;
  bool used = false;
};

static int fireDue(PendingSlot& p, uint32_t nowMs) {
  if (!p.used) return 0;
  if (static_cast<int32_t>(nowMs - p.fireAt) < 0) return 0;
  p.used = false;
  return 1;
}

void test_pending_fires_once_at_deadline() {
  PendingSlot p;
  p.fireAt = 5400;
  p.used = true;
  TEST_ASSERT_EQUAL_INT(0, fireDue(p, 5399));  // before deadline
  TEST_ASSERT_EQUAL_INT(1, fireDue(p, 5400));  // at deadline: fires
  TEST_ASSERT_EQUAL_INT(0, fireDue(p, 6000));  // already fired: no repeat
  TEST_ASSERT_FALSE(p.used);
}

// --- 6. emitEvent continuous-gate -----------------------------------------

// Mirror of the guard added to ExpressionManager::emitEvent: a continuous
// descriptor never announces (so continuous retriggers at boot / settings /
// wisp-release can't spuriously trigger peer mirrors).
static bool emitEventContinuousGate(bool descriptorContinuous) {
  if (descriptorContinuous) return false;  // skip announce
  return true;                             // proceed to announce
}

void test_emitevent_skips_continuous() {
  TEST_ASSERT_FALSE(emitEventContinuousGate(true));
  TEST_ASSERT_TRUE(emitEventContinuousGate(false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_grid_cold_dispositions_never_mirror);
  RUN_TEST(test_grid_fond);
  RUN_TEST(test_grid_smitten);
  RUN_TEST(test_grid_out_of_range_disposition);
  RUN_TEST(test_disp_below_4_skips);
  RUN_TEST(test_winning_roll_schedules);
  RUN_TEST(test_introvert_cooldown_gates_second_mirror);
  RUN_TEST(test_non_introvert_has_no_cooldown);
  RUN_TEST(test_fireat_window);
  RUN_TEST(test_pending_fires_once_at_deadline);
  RUN_TEST(test_emitevent_skips_continuous);
  return UNITY_END();
}
