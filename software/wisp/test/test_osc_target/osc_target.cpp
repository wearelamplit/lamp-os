#include <unity.h>

#include <cstring>

#include "osc/osc_freshness.hpp"
#include "osc/osc_target.hpp"

using wisp::OscMode;
using wisp::OscRole;
using wisp::OscTarget;

void setUp() {}
void tearDown() {}

namespace {

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

// A resend of the stored target never resets the stream.
void test_same_target_keeps() {
  TEST_ASSERT_FALSE(wisp::oscTargetResets(maestro(3), maestro(3)));
  TEST_ASSERT_FALSE(wisp::oscTargetResets(straylight(OscRole::Tertiary),
                                          straylight(OscRole::Tertiary)));
}

void test_slot_change_resets() {
  TEST_ASSERT_TRUE(wisp::oscTargetResets(maestro(1), maestro(10)));
  TEST_ASSERT_TRUE(wisp::oscTargetResets(maestro(10), maestro(2)));
}

void test_mode_change_resets_both_ways() {
  TEST_ASSERT_TRUE(wisp::oscTargetResets(straylight(OscRole::Primary), maestro(1)));
  TEST_ASSERT_TRUE(wisp::oscTargetResets(maestro(1), straylight(OscRole::Primary)));
}

// A Straylight role change keeps the old role painting (today's rule), and
// the stored Maestro slot is not part of a Straylight target.
void test_straylight_role_or_slot_change_keeps() {
  TEST_ASSERT_FALSE(wisp::oscTargetResets(straylight(OscRole::Primary),
                                          straylight(OscRole::Fill)));
  OscTarget slot7 = straylight(OscRole::Primary);
  slot7.slot = 7;
  TEST_ASSERT_FALSE(
      wisp::oscTargetResets(straylight(OscRole::Primary), slot7));
}

// In Maestro the role is not part of the target.
void test_maestro_role_change_keeps() {
  TEST_ASSERT_FALSE(wisp::oscTargetResets(maestro(5, OscRole::Primary),
                                          maestro(5, OscRole::Secondary)));
}

void test_slot_from_text() {
  uint8_t slot = 0;
  TEST_ASSERT_TRUE(wisp::maestroSlotFromText("1", slot));
  TEST_ASSERT_EQUAL_UINT8(1, slot);
  TEST_ASSERT_TRUE(wisp::maestroSlotFromText("10", slot));
  TEST_ASSERT_EQUAL_UINT8(10, slot);
  for (const char* bad : {"0", "11", "3x", "x3", "", " 3", "-1", "+3",
                          "99999999999", "4294967299"}) {
    slot = 42;
    TEST_ASSERT_FALSE_MESSAGE(wisp::maestroSlotFromText(bad, slot), bad);
    TEST_ASSERT_EQUAL_UINT8(42, slot);
  }
  TEST_ASSERT_FALSE(wisp::maestroSlotFromText(nullptr, slot));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_maestro_listens_on_its_slot);
  RUN_TEST(test_straylight_listens_on_its_role);
  RUN_TEST(test_stale_window_per_mode);
  RUN_TEST(test_mode_names_round_trip);
  RUN_TEST(test_corrupt_raw_values_coerce);
  RUN_TEST(test_same_target_keeps);
  RUN_TEST(test_slot_change_resets);
  RUN_TEST(test_mode_change_resets_both_ways);
  RUN_TEST(test_straylight_role_or_slot_change_keeps);
  RUN_TEST(test_maestro_role_change_keeps);
  RUN_TEST(test_slot_from_text);
  return UNITY_END();
}
