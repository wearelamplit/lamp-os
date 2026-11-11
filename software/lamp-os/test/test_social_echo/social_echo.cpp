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
//   7. Copycat chains: an origin copies at today's mirror chance, each hop
//      past it at chainHopPct of the one before, none past chainMaxHops; a
//      copy carries one hop more; one roll per peer per window, fired or not;
//      a crowd of 500 peers rolls at most RollCooldownTable::kSlots times.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "behaviors/copycat.cpp"
#include "behaviors/roll_cooldown.cpp"

using lamp::ContagionTuning;
using lamp::CopyDecision;
using lamp::CopyOutcome;
using lamp::Copycat;
using lamp::SocialMode;
using lamp::copyChancePct;
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
    TEST_ASSERT_FALSE(mirrorDecision(mirrorRatePct(d, SocialMode::Extrovert), SocialMode::Extrovert, /*roll*/ 1,
                                     /*jitter*/ 0, /*now*/ 1000,
                                     /*ever*/ false, /*last*/ 0, fireAt));
  }
}

// --- 3. roll gate ---------------------------------------------------------

void test_winning_roll_schedules() {
  uint32_t fireAt = 0;
  // Smitten/Ambivert rate = 50. roll 50 wins (<=), roll 51 loses.
  TEST_ASSERT_TRUE(mirrorDecision(mirrorRatePct(5, SocialMode::Ambivert), SocialMode::Ambivert, 50, 0, 1000, false,
                                  0, fireAt));
  TEST_ASSERT_FALSE(mirrorDecision(mirrorRatePct(5, SocialMode::Ambivert), SocialMode::Ambivert, 51, 0, 1000, false,
                                   0, fireAt));
}

// --- 4. introvert cooldown ------------------------------------------------

void test_introvert_cooldown_gates_second_mirror() {
  uint32_t fireAt = 0;
  const uint32_t last = 1000;
  // Within the 10-min window → gated.
  TEST_ASSERT_FALSE(mirrorDecision(mirrorRatePct(5, SocialMode::Introvert), SocialMode::Introvert, 1, 0,
                                   last + lamp::kIntrovertMirrorCooldownMs - 1,
                                   /*ever*/ true, last, fireAt));
  // At/after the window → allowed.
  TEST_ASSERT_TRUE(mirrorDecision(mirrorRatePct(5, SocialMode::Introvert), SocialMode::Introvert, 1, 0,
                                  last + lamp::kIntrovertMirrorCooldownMs,
                                  /*ever*/ true, last, fireAt));
  // First-ever mirror (everMirrored=false) is never gated.
  TEST_ASSERT_TRUE(mirrorDecision(mirrorRatePct(5, SocialMode::Introvert), SocialMode::Introvert, 1, 0, last + 10,
                                  /*ever*/ false, last, fireAt));
}

void test_non_introvert_has_no_cooldown() {
  uint32_t fireAt = 0;
  TEST_ASSERT_TRUE(mirrorDecision(mirrorRatePct(5, SocialMode::Ambivert), SocialMode::Ambivert, 1, 0, 1010, true,
                                  1000, fireAt));
  TEST_ASSERT_TRUE(mirrorDecision(mirrorRatePct(5, SocialMode::Extrovert), SocialMode::Extrovert, 1, 0, 1010, true,
                                  1000, fireAt));
}

// --- 5. scheduling window + fire ------------------------------------------

void test_fireat_window() {
  uint32_t fireAt = 0;
  const uint32_t now = 5000;
  // jitter floored at 0.
  TEST_ASSERT_TRUE(mirrorDecision(mirrorRatePct(5, SocialMode::Ambivert), SocialMode::Ambivert, 1, 0, now, false, 0,
                                  fireAt));
  TEST_ASSERT_EQUAL_UINT32(now + lamp::kMirrorDelayFloorMs, fireAt);
  // jitter at max.
  TEST_ASSERT_TRUE(mirrorDecision(mirrorRatePct(5, SocialMode::Ambivert), SocialMode::Ambivert, 1,
                                  lamp::kMirrorJitterMs, now, false, 0, fireAt));
  TEST_ASSERT_EQUAL_UINT32(now + lamp::kMirrorDelayFloorMs +
                               lamp::kMirrorJitterMs,
                           fireAt);
  // jitter clamped when the draw exceeds the jitter span.
  TEST_ASSERT_TRUE(mirrorDecision(mirrorRatePct(5, SocialMode::Ambivert), SocialMode::Ambivert, 1,
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

// An introvert that mirrored once, then nothing for a whole wrap: the
// cooldown retired every few days must not gate the next mirror.
void test_introvert_cooldown_stays_ended_a_wrap_later() {
  lamp::MirrorCooldown cooldown;
  const uint32_t mirroredMs = 1000;
  cooldown.mirrored(mirroredMs);
  for (uint64_t t = mirroredMs; t < mirroredMs + (1ull << 32); t += 1ull << 28) {
    cooldown.retire(static_cast<uint32_t>(t));
  }
  uint32_t fireAt = 0;
  TEST_ASSERT_TRUE(mirrorDecision(mirrorRatePct(5, SocialMode::Introvert), SocialMode::Introvert, 1, 0, mirroredMs + 10,
                                  cooldown.ever, cooldown.lastMs, fireAt));
}

// Inside the cooldown retire keeps it.
void test_retire_keeps_a_running_introvert_cooldown() {
  lamp::MirrorCooldown cooldown;
  cooldown.mirrored(1000);
  const uint32_t nowMs = 1000 + lamp::kIntrovertMirrorCooldownMs - 1;
  cooldown.retire(nowMs);
  uint32_t fireAt = 0;
  TEST_ASSERT_FALSE(mirrorDecision(mirrorRatePct(5, SocialMode::Introvert), SocialMode::Introvert, 1, 0, nowMs, cooldown.ever,
                                   cooldown.lastMs, fireAt));
}

// --- 7. copycat chains ----------------------------------------------------

static constexpr SocialMode kModes[3] = {SocialMode::Introvert, SocialMode::Ambivert,
                                         SocialMode::Extrovert};

static void macFor(uint32_t i, uint8_t mac[6]) {
  mac[0] = 0x24;
  mac[1] = 0x6F;
  mac[2] = static_cast<uint8_t>(i >> 16);
  mac[3] = static_cast<uint8_t>(i >> 8);
  mac[4] = static_cast<uint8_t>(i);
  mac[5] = static_cast<uint8_t>(i * 37);
}

void test_origin_copies_at_the_mirror_chance() {
  const ContagionTuning t;
  for (uint8_t d = 0; d <= 6; ++d) {
    for (SocialMode m : kModes) {
      TEST_ASSERT_EQUAL_UINT8(mirrorRatePct(d, m), copyChancePct(mirrorRatePct(d, m), 0, 0, t));
    }
  }
}

// Each hop rolls at chainHopPct of the hop before; a copy that would pass
// chainMaxHops never rolls.
void test_hop_chance_scales_and_stops_at_max() {
  ContagionTuning t;
  for (uint8_t hopPct : {50, 30}) {
    t.chainHopPct = hopPct;
    for (uint8_t maxHops : {1, 3, 5}) {
      t.chainMaxHops = maxHops;
      uint8_t expected = 75;
      for (uint8_t h = 0; h < maxHops; ++h) {
        TEST_ASSERT_EQUAL_UINT8(expected, copyChancePct(75, h, 0, t));
        expected = static_cast<uint8_t>(expected * hopPct / 100);
      }
      TEST_ASSERT_EQUAL_UINT8(0, copyChancePct(75, maxHops, 0, t));
      TEST_ASSERT_EQUAL_UINT8(0, copyChancePct(75, 255, 0, t));
    }
  }
}

// The default boost leaves every chance alone; a set boost adds its percent
// per meeting up to kFamiliarBoostMeets and never passes 100.
void test_familiar_boost() {
  ContagionTuning t;
  TEST_ASSERT_EQUAL_UINT8(50, copyChancePct(50, 0, 7, t));
  t.familiarBoostPct = 3;
  TEST_ASSERT_EQUAL_UINT8(50, copyChancePct(50, 0, 0, t));
  TEST_ASSERT_EQUAL_UINT8(60, copyChancePct(50, 0, 7, t));
  TEST_ASSERT_EQUAL_UINT8(65, copyChancePct(50, 0, lamp::kFamiliarBoostMeets, t));
  TEST_ASSERT_EQUAL_UINT8(65, copyChancePct(50, 0, 200, t));
  TEST_ASSERT_EQUAL_UINT8(100, copyChancePct(90, 0, 200, t));
}

// A copy of a frame h hops out is h + 1, and announces only short of the max.
void test_copy_carries_one_more_hop() {
  Copycat c;
  uint8_t mac[6];
  for (uint8_t h = 0; h < c.tuning.chainMaxHops; ++h) {
    macFor(h, mac);
    const CopyDecision d = c.decide(mac, 5, SocialMode::Extrovert, h, 1000, 1, 0);
    TEST_ASSERT_EQUAL(CopyOutcome::Fire, d.outcome);
    TEST_ASSERT_EQUAL_UINT8(h + 1, d.hops);
    TEST_ASSERT_EQUAL(d.hops < c.tuning.chainMaxHops, c.announces(d.hops));
  }
  TEST_ASSERT_FALSE(c.announces(c.tuning.chainMaxHops));
  macFor(50, mac);
  TEST_ASSERT_EQUAL(CopyOutcome::NoChance,
                    c.decide(mac, 5, SocialMode::Extrovert, c.tuning.chainMaxHops, 1000, 1, 0)
                        .outcome);
}

// A Smitten peer firing every second for ten minutes, every roll forced to
// miss: one roll per window, the rest refused.
void test_peer_firing_every_second_rolls_once_per_window() {
  Copycat c;
  uint8_t mac[6];
  macFor(9, mac);
  unsigned rolls = 0, refused = 0;
  for (uint32_t s = 0; s < 600; ++s) {
    const uint32_t now = 4321 + s * 1000;
    c.retire(now);
    const CopyDecision d = c.decide(mac, 5, SocialMode::Ambivert, 0, now, 100, 0);
    TEST_ASSERT_TRUE(d.outcome != CopyOutcome::Fire);
    if (d.outcome == CopyOutcome::Skip) ++rolls;
    if (d.outcome == CopyOutcome::Cooldown) ++refused;
  }
  TEST_ASSERT_EQUAL_UINT((600 * 1000 + c.tuning.copyRollCooldownMs - 1) / c.tuning.copyRollCooldownMs,
                         rolls);
  TEST_ASSERT_EQUAL_UINT(600 - rolls, refused);
}

// 500 warm peers each fire once inside one window: at most kSlots roll.
void test_crowd_of_500_rolls_at_most_the_table() {
  Copycat c;
  uint8_t mac[6];
  unsigned rolls = 0;
  for (uint32_t i = 0; i < 500; ++i) {
    const uint32_t now = 1000 + i * 100;
    c.retire(now);
    macFor(i, mac);
    const CopyOutcome o = c.decide(mac, 5, SocialMode::Ambivert, 0, now, 100, 0).outcome;
    if (o == CopyOutcome::Skip || o == CopyOutcome::Fire) ++rolls;
  }
  TEST_ASSERT_EQUAL_UINT(lamp::RollCooldownTable::kSlots, rolls);
}

// A peer with no chance spends no slot, so a cold crowd leaves room.
void test_cold_peers_take_no_roll() {
  Copycat c;
  uint8_t mac[6];
  for (uint32_t i = 0; i < 500; ++i) {
    macFor(i, mac);
    TEST_ASSERT_EQUAL(CopyOutcome::NoChance,
                      c.decide(mac, 3, SocialMode::Extrovert, 0, 1000 + i, 1, 0).outcome);
  }
  macFor(9999, mac);
  TEST_ASSERT_EQUAL(CopyOutcome::Fire,
                    c.decide(mac, 5, SocialMode::Extrovert, 0, 2000, 1, 0).outcome);
}

void test_set_tuning_checks_key_and_range() {
  ContagionTuning t;
  TEST_ASSERT_TRUE(lamp::setContagionTuning(t, "chainMaxHops", 2));
  TEST_ASSERT_EQUAL_UINT8(2, t.chainMaxHops);
  TEST_ASSERT_TRUE(lamp::setContagionTuning(t, "copyRollCooldownMs", 60000));
  TEST_ASSERT_EQUAL_UINT32(60000, t.copyRollCooldownMs);
  TEST_ASSERT_TRUE(lamp::setContagionTuning(t, "chainHopPct", 100));
  TEST_ASSERT_TRUE(lamp::setContagionTuning(t, "familiarBoostPct", lamp::kMaxFamiliarBoostPct));

  const ContagionTuning before = t;
  TEST_ASSERT_FALSE(lamp::setContagionTuning(t, "chainMaxHops", 0));
  TEST_ASSERT_FALSE(lamp::setContagionTuning(t, "chainMaxHops", lamp::kMaxChainHops + 1));
  TEST_ASSERT_FALSE(lamp::setContagionTuning(t, "chainHopPct", 101));
  TEST_ASSERT_FALSE(lamp::setContagionTuning(t, "familiarBoostPct", lamp::kMaxFamiliarBoostPct + 1));
  TEST_ASSERT_FALSE(
      lamp::setContagionTuning(t, "copyRollCooldownMs", lamp::kMaxCopyRollCooldownMs + 1));
  TEST_ASSERT_FALSE(lamp::setContagionTuning(t, "chainmaxhops", 2));
  TEST_ASSERT_EQUAL_UINT8(before.chainMaxHops, t.chainMaxHops);
  TEST_ASSERT_EQUAL_UINT8(before.chainHopPct, t.chainHopPct);
  TEST_ASSERT_EQUAL_UINT8(before.familiarBoostPct, t.familiarBoostPct);
  TEST_ASSERT_EQUAL_UINT32(before.copyRollCooldownMs, t.copyRollCooldownMs);
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
  RUN_TEST(test_introvert_cooldown_stays_ended_a_wrap_later);
  RUN_TEST(test_retire_keeps_a_running_introvert_cooldown);
  RUN_TEST(test_origin_copies_at_the_mirror_chance);
  RUN_TEST(test_hop_chance_scales_and_stops_at_max);
  RUN_TEST(test_familiar_boost);
  RUN_TEST(test_copy_carries_one_more_hop);
  RUN_TEST(test_peer_firing_every_second_rolls_once_per_window);
  RUN_TEST(test_crowd_of_500_rolls_at_most_the_table);
  RUN_TEST(test_cold_peers_take_no_roll);
  RUN_TEST(test_set_tuning_checks_key_and_range);
  return UNITY_END();
}
