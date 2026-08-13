// Real-code pin for the greeting-profile mapping. Exercises the SHIPPED
// greeting_profiles.hpp (kByTier ordering + profileFor + greetingTuningFor),
// not a mirror, so a transposition in kByTier or a mis-tiered cell fails here
// even though the firmware-free mirror in test_personality_engine would pass.

#include <unity.h>

#include <cstdint>

#include "core/greeting_profiles.hpp"

using lamp::Easing;
using lamp::GreetingTuning;
using lamp::SocialMode;
using lamp::greetingTuningFor;
using lamp::kPulseCountContinuous;
namespace gp = lamp::greeting_profiles;

void setUp() {}
void tearDown() {}

// The openness ladder [0..7] must map 1:1 to the profile table, in order.
// A swapped pair here is exactly the bug the mirror can't see.
void test_tier_ordering_pins_kByTier() {
  TEST_ASSERT_EQUAL_UINT32(540,  gp::profileForTier(0).total);  // Snub
  TEST_ASSERT_TRUE(gp::profileForTier(0).snub);
  TEST_ASSERT_EQUAL_UINT8(191,   gp::profileForTier(0).pulseBackStrength);

  TEST_ASSERT_EQUAL_UINT32(570,  gp::profileForTier(1).total);  // PartialSnub
  TEST_ASSERT_TRUE(gp::profileForTier(1).snub);
  TEST_ASSERT_EQUAL_UINT8(165,   gp::profileForTier(1).pulseBackStrength);

  TEST_ASSERT_EQUAL_UINT32(1080, gp::profileForTier(2).total);  // Minimal
  TEST_ASSERT_EQUAL_UINT32(1278, gp::profileForTier(3).total);  // Standard
  TEST_ASSERT_EQUAL_UINT32(1176, gp::profileForTier(4).total);  // Gentle

  TEST_ASSERT_EQUAL_UINT32(1404, gp::profileForTier(5).total);  // Warm
  TEST_ASSERT_EQUAL_UINT8(50,    gp::profileForTier(5).pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(kPulseCountContinuous, gp::profileForTier(5).pulseBackCount);

  TEST_ASSERT_EQUAL_UINT32(1512, gp::profileForTier(6).total);  // Enthused
  TEST_ASSERT_EQUAL_UINT8(65,    gp::profileForTier(6).pulseBackStrength);

  TEST_ASSERT_EQUAL_UINT32(1620, gp::profileForTier(7).total);  // Effusive
  TEST_ASSERT_EQUAL_UINT8(80,    gp::profileForTier(7).pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(kPulseCountContinuous, gp::profileForTier(7).pulseBackCount);
}

// All 15 (mode × disposition) cells through the real greetingTuningFor.
void test_greeting_tuning_matrix_real_code() {
  struct Row {
    SocialMode mode;
    uint8_t disposition;
    uint32_t total;
    uint32_t easeIn;
    uint32_t hold;
    uint32_t fadeOut;
    uint8_t  pulseBack;
    uint8_t  pulseCount;
    bool     snub;
    uint16_t breath;
  };
  const uint8_t cont = kPulseCountContinuous;
  const Row rows[] = {
    // Salty → Snub in every mode
    {SocialMode::Introvert, 1,  540, 120,  300, 120, 191, 1, true,  120},
    {SocialMode::Ambivert,  1,  540, 120,  300, 120, 191, 1, true,  120},
    {SocialMode::Extrovert, 1,  540, 120,  300, 120, 191, 1, true,  120},
    // Wary → PartialSnub in every mode
    {SocialMode::Introvert, 2,  570, 120,  300, 150, 165, 1, true,  120},
    {SocialMode::Ambivert,  2,  570, 120,  300, 150, 165, 1, true,  120},
    {SocialMode::Extrovert, 2,  570, 120,  300, 150, 165, 1, true,  120},
    // Neutral
    {SocialMode::Introvert, 3, 1080, 180,  780, 120,   0, 0, false, 120},
    {SocialMode::Ambivert,  3, 1278, 168,  960, 150,   0, 0, false, 120},
    {SocialMode::Extrovert, 3, 1278, 168,  960, 150,   0, 0, false, 120},
    // Fond
    {SocialMode::Introvert, 4, 1176, 156,  840, 180,   0,    0, false, 120},
    {SocialMode::Ambivert,  4, 1404, 144, 1020, 240,  50, cont, false, 300},
    {SocialMode::Extrovert, 4, 1512, 132, 1080, 300,  65, cont, false, 240},
    // Smitten
    {SocialMode::Introvert, 5, 1404, 144, 1020, 240,  50, cont, false, 300},
    {SocialMode::Ambivert,  5, 1512, 132, 1080, 300,  65, cont, false, 240},
    {SocialMode::Extrovert, 5, 1620, 120, 1140, 360,  80, cont, false, 180},
  };
  for (const auto& r : rows) {
    const GreetingTuning t = greetingTuningFor(r.mode, r.disposition);
    TEST_ASSERT_EQUAL_UINT32(r.total,   t.totalFrames);
    TEST_ASSERT_EQUAL_UINT32(r.easeIn,  t.easeInFrames);
    TEST_ASSERT_EQUAL_UINT32(r.hold,    t.holdFrames);
    TEST_ASSERT_EQUAL_UINT32(r.fadeOut, t.fadeOutFrames);
    TEST_ASSERT_EQUAL_UINT8(r.pulseBack,  t.pulseBackStrength);
    TEST_ASSERT_EQUAL_UINT8(r.pulseCount, t.pulseBackCount);
    TEST_ASSERT_EQUAL(r.snub, t.snub);
    TEST_ASSERT_EQUAL_UINT16(r.breath, t.breathCycleFrames);
    // Phase sum is internally consistent.
    TEST_ASSERT_EQUAL_UINT32(t.easeInFrames + t.holdFrames + t.fadeOutFrames,
                             t.totalFrames);
  }
}

// Warm family swells in + smooths out; neutral/snub are Smooth both ways.
void test_greeting_curves_real_code() {
  TEST_ASSERT_EQUAL(Easing::Swell,  greetingTuningFor(SocialMode::Ambivert, 4).easeInCurve);
  TEST_ASSERT_EQUAL(Easing::Smooth, greetingTuningFor(SocialMode::Ambivert, 4).easeOutCurve);
  TEST_ASSERT_EQUAL(Easing::Smooth, greetingTuningFor(SocialMode::Ambivert, 3).easeInCurve);
  TEST_ASSERT_EQUAL(Easing::Smooth, greetingTuningFor(SocialMode::Ambivert, 1).easeInCurve);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tier_ordering_pins_kByTier);
  RUN_TEST(test_greeting_tuning_matrix_real_code);
  RUN_TEST(test_greeting_curves_real_code);
  return UNITY_END();
}
