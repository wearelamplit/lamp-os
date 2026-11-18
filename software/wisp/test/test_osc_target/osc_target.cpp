#include <unity.h>

#include <cstring>

#include "osc/osc_target.hpp"

using wisp::OscMode;
using wisp::OscRole;
using wisp::OscStreamGate;
using wisp::OscTarget;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kHourMs = 3600000;

OscTarget straylight(OscRole role) {
  OscTarget t;
  t.mode = OscMode::Straylight;
  t.role = role;
  t.slot = 4;
  return t;
}

OscTarget maestro(uint8_t slot, OscRole role = OscRole::Primary) {
  OscTarget t;
  t.mode = OscMode::Maestro;
  t.role = role;
  t.slot = slot;
  return t;
}

void assertAddress(const char* want, const OscTarget& t) {
  char out[wisp::kOscTargetAddressCap];
  wisp::oscTargetAddress(t, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(want, out);
}

// Makes the gate's stream live at `nowMs`.
void goLive(OscStreamGate& g, uint32_t nowMs) {
  g.freshness().onPalette(nowMs);
}

}  // namespace

void test_maestro_listens_on_its_slot() {
  assertAddress("/palette/live/3", maestro(3));
  assertAddress("/palette/live/10", maestro(10));
  assertAddress("/palette/live/1", maestro(1, OscRole::Fill));
}

// Straylight keeps today's address per role, whatever slot is stored.
void test_straylight_listens_on_its_role() {
  for (uint8_t r = 0; r < wisp::kOscRoleCount; ++r) {
    const OscRole role = static_cast<OscRole>(r);
    assertAddress(wisp::oscPaletteAddress(role), straylight(role));
  }
}

void test_stale_window_per_mode() {
  TEST_ASSERT_EQUAL_UINT32(wisp::OscFreshness::kStaleMs,
                           wisp::oscTargetStaleMs(straylight(OscRole::Fill)));
  TEST_ASSERT_EQUAL_UINT32(0, wisp::oscTargetStaleMs(maestro(7)));
}

void test_mode_names_round_trip() {
  OscMode m = OscMode::Straylight;
  TEST_ASSERT_TRUE(wisp::oscModeFromName("maestro", m));
  TEST_ASSERT_EQUAL_INT((int)OscMode::Maestro, (int)m);
  TEST_ASSERT_EQUAL_STRING("maestro", wisp::oscModeName(m));
  TEST_ASSERT_TRUE(wisp::oscModeFromName("straylight", m));
  TEST_ASSERT_EQUAL_INT((int)OscMode::Straylight, (int)m);
  TEST_ASSERT_EQUAL_STRING("straylight", wisp::oscModeName(m));
  for (const char* bad : {"custom", "Maestro", "", "aurora"}) {
    TEST_ASSERT_FALSE(wisp::oscModeFromName(bad, m));
  }
  TEST_ASSERT_FALSE(wisp::oscModeFromName(nullptr, m));
}

void test_corrupt_raw_values_coerce() {
  TEST_ASSERT_EQUAL_INT((int)OscMode::Maestro, (int)wisp::coerceOscMode(1));
  TEST_ASSERT_EQUAL_INT((int)OscMode::Straylight, (int)wisp::coerceOscMode(9));
  TEST_ASSERT_EQUAL_INT((int)OscMode::Straylight, (int)wisp::coerceOscMode(-1));
  TEST_ASSERT_EQUAL_UINT8(7, wisp::coerceMaestroSlot(7));
  TEST_ASSERT_EQUAL_UINT8(10, wisp::coerceMaestroSlot(10));
  TEST_ASSERT_EQUAL_UINT8(1, wisp::coerceMaestroSlot(0));
  TEST_ASSERT_EQUAL_UINT8(1, wisp::coerceMaestroSlot(11));
  TEST_ASSERT_EQUAL_UINT8(1, wisp::coerceMaestroSlot(255));
}

// Boot never holds, and the seeded target's window is the one in force.
void test_seed_never_holds_and_sets_window() {
  OscStreamGate g;
  g.seed(maestro(3));
  goLive(g, 1000);
  TEST_ASSERT_FALSE(g.freshness().tick(1000 + kHourMs));
  TEST_ASSERT_TRUE(g.freshness().live());

  OscStreamGate s;
  s.seed(maestro(3));
  s.seed(straylight(OscRole::Primary));
  goLive(s, 1000);
  TEST_ASSERT_TRUE(s.freshness().tick(1000 + wisp::OscFreshness::kStaleMs));
}

// A resend of the stored target never resets the stream.
void test_same_target_keeps() {
  OscStreamGate g;
  g.seed(maestro(3));
  TEST_ASSERT_FALSE(g.apply(maestro(3), true));
  TEST_ASSERT_FALSE(g.apply(maestro(3), true));
  g.seed(straylight(OscRole::Tertiary));
  TEST_ASSERT_FALSE(g.apply(straylight(OscRole::Tertiary), true));
}

void test_slot_change_holds() {
  OscStreamGate g;
  g.seed(maestro(1));
  TEST_ASSERT_TRUE(g.apply(maestro(10), true));
  TEST_ASSERT_TRUE(g.apply(maestro(2), true));
}

void test_mode_change_holds_both_ways() {
  OscStreamGate g;
  g.seed(straylight(OscRole::Primary));
  TEST_ASSERT_TRUE(g.apply(maestro(1), true));
  TEST_ASSERT_TRUE(g.apply(straylight(OscRole::Primary), true));
}

// A Straylight role change keeps the old role painting (today's rule).
void test_straylight_role_change_keeps() {
  OscStreamGate g;
  g.seed(straylight(OscRole::Primary));
  TEST_ASSERT_FALSE(g.apply(straylight(OscRole::Fill), true));
}

// In Maestro the role is not part of the target.
void test_maestro_role_change_keeps() {
  OscStreamGate g;
  g.seed(maestro(5, OscRole::Primary));
  TEST_ASSERT_FALSE(g.apply(maestro(5, OscRole::Secondary), true));
}

// Off or Manual: the target is stored but nothing holds, and the window
// follows it, so entering Osc later starts on the right footing.
void test_not_osc_never_holds_and_records() {
  OscStreamGate g;
  g.seed(straylight(OscRole::Primary));
  TEST_ASSERT_FALSE(g.apply(maestro(6), false));
  TEST_ASSERT_FALSE(g.apply(maestro(6), true));
  TEST_ASSERT_TRUE(g.apply(maestro(7), true));
  goLive(g, 0);
  TEST_ASSERT_FALSE(g.freshness().tick(kHourMs));
}

// The window follows every apply, not only the seed.
void test_apply_sets_window() {
  OscStreamGate g;
  g.seed(maestro(2));
  g.apply(straylight(OscRole::Secondary), true);
  goLive(g, 500);
  TEST_ASSERT_TRUE(g.freshness().tick(500 + wisp::OscFreshness::kStaleMs));
  g.apply(maestro(2), true);
  goLive(g, 500);
  TEST_ASSERT_FALSE(g.freshness().tick(500 + kHourMs));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_maestro_listens_on_its_slot);
  RUN_TEST(test_straylight_listens_on_its_role);
  RUN_TEST(test_stale_window_per_mode);
  RUN_TEST(test_mode_names_round_trip);
  RUN_TEST(test_corrupt_raw_values_coerce);
  RUN_TEST(test_seed_never_holds_and_sets_window);
  RUN_TEST(test_same_target_keeps);
  RUN_TEST(test_slot_change_holds);
  RUN_TEST(test_mode_change_holds_both_ways);
  RUN_TEST(test_straylight_role_change_keeps);
  RUN_TEST(test_maestro_role_change_keeps);
  RUN_TEST(test_not_osc_never_holds_and_records);
  RUN_TEST(test_apply_sets_window);
  return UNITY_END();
}
