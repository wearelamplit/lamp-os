#include <unity.h>
#include <array>
#include <set>
#include <vector>
#include "../../src/lamps/lioness/lions_director.hpp"

using namespace lamp::lioness;

static Mac mac(uint8_t tag) { Mac m{}; m[5] = tag; return m; }

static void assertDistinctPresent(const LionsDirector& d, const std::vector<Mac>& near) {
  TEST_ASSERT_FALSE(d.current[0] == d.current[1]);
  TEST_ASSERT_FALSE(d.current[1] == d.current[2]);
  TEST_ASSERT_FALSE(d.current[0] == d.current[2]);
  for (int i = 0; i < 3; ++i) TEST_ASSERT_TRUE(macIn(d.current[i], near));
}

void setUp() {}
void tearDown() {}

void test_zero_nearby_all_idle() {
  LionsDirector d;
  d.tick(1000, {});
  TEST_ASSERT_TRUE(d.current[0] == kIdle);
  TEST_ASSERT_TRUE(d.current[1] == kIdle);
  TEST_ASSERT_TRUE(d.current[2] == kIdle);
}

void test_one_nearby_all_same() {
  LionsDirector d;
  d.tick(1000, {mac(1)});
  TEST_ASSERT_TRUE(d.current[0] == mac(1));
  TEST_ASSERT_TRUE(d.current[1] == mac(1));
  TEST_ASSERT_TRUE(d.current[2] == mac(1));
}

// Two nearby: two lions show the pair, the third holds idle (Main).
void test_two_nearby_pair_plus_idle() {
  LionsDirector d;
  std::vector<Mac> near = {mac(1), mac(2)};
  d.tick(1000, near);
  TEST_ASSERT_TRUE(d.current[0] == mac(1));
  TEST_ASSERT_TRUE(d.current[1] == mac(2));
  TEST_ASSERT_TRUE(d.current[2] == kIdle);
  // Stays put over time; nothing to walk with only two lamps.
  d.tick(1000 + 3 * kStaggerPeriodMs, near);
  TEST_ASSERT_TRUE(d.current[0] == mac(1));
  TEST_ASSERT_TRUE(d.current[1] == mac(2));
  TEST_ASSERT_TRUE(d.current[2] == kIdle);
}

// Exactly three nearby: seed is one distinct lamp each.
void test_three_nearby_distinct_one_each() {
  LionsDirector d;
  d.tick(1000, {mac(1), mac(2), mac(3)});
  TEST_ASSERT_TRUE(d.current[0] == mac(1));
  TEST_ASSERT_TRUE(d.current[1] == mac(2));
  TEST_ASSERT_TRUE(d.current[2] == mac(3));
}

// Entry seeds three staggered deadlines, one phase-step apart, none at the
// entry instant, and the tick right after re-rolls no lion.
void test_entry_seeds_staggered_deadlines() {
  LionsDirector d;
  std::vector<Mac> near = {mac(1), mac(2), mac(3), mac(4), mac(5)};
  const uint32_t E = 20000;
  d.tick(E, near);
  TEST_ASSERT_EQUAL_UINT32(E + 1 * kStaggerPhaseMs, d.nextMs[0]);
  TEST_ASSERT_EQUAL_UINT32(E + 2 * kStaggerPhaseMs, d.nextMs[1]);
  TEST_ASSERT_EQUAL_UINT32(E + 3 * kStaggerPhaseMs, d.nextMs[2]);
  TEST_ASSERT_EQUAL_UINT8(0, d.tick(E + 10, near));   // no lion due yet
}

// >=3 nearby, over two full periods: (a) every lamp in the list gets shown by
// some lion (walk-through-all), (b) the trio is always distinct, (c) no two
// lions ever switch on the same tick (staggered offset), and switches do occur.
void test_walk_distinct_and_staggered() {
  LionsDirector d;
  std::vector<Mac> near = {mac(1), mac(2), mac(3), mac(4), mac(5)};
  const uint32_t E = 1000;
  d.tick(E, near);   // seed (not counted as a walk switch)

  std::set<Mac> shown{d.current[0], d.current[1], d.current[2]};
  bool allThreeAtOnce = false;
  int switchTicks = 0;
  for (uint32_t t = E + 50; t <= E + 4 * kStaggerPeriodMs; t += 50) {
    const uint8_t mask = d.tick(t, near);
    if (mask != 0) ++switchTicks;
    if (mask == 0x7) allThreeAtOnce = true;
    shown.insert(d.current[0]);
    shown.insert(d.current[1]);
    shown.insert(d.current[2]);
    TEST_ASSERT_FALSE(d.current[0] == d.current[1]);
    TEST_ASSERT_FALSE(d.current[1] == d.current[2]);
    TEST_ASSERT_FALSE(d.current[0] == d.current[2]);
  }
  TEST_ASSERT_EQUAL_UINT32(5, shown.size());          // every nearby lamp shown
  TEST_ASSERT_GREATER_OR_EQUAL_INT(2, switchTicks);   // switches occur, staggered
  TEST_ASSERT_FALSE(allThreeAtOnce);                  // never all three on one tick
}

// A lamp dropping out of the list is replaced by the next lamp in the walk; the
// trio stays distinct.
void test_dropout_replaced_stays_distinct() {
  LionsDirector d;
  std::vector<Mac> near = {mac(1), mac(2), mac(3), mac(4)};
  d.tick(1000, near);   // seed -> {1,2,3}
  std::vector<Mac> shrunk = {mac(1), mac(4)};   // 2 and 3 (shown) leave; now <3
  d.tick(1100, shrunk);
  TEST_ASSERT_TRUE(d.current[0] == mac(1));
  TEST_ASSERT_TRUE(d.current[1] == mac(4));
  TEST_ASSERT_TRUE(d.current[2] == kIdle);
}

// A shown peer leaving while the roster stays >=3 forces its lion onto a new
// distinct lamp via the !macIn replacement branch; a fresh lamp appearing
// mid-walk joins the walk. The trio stays 3-distinct throughout the churn.
void test_churn_keeps_trio_distinct() {
  LionsDirector d;
  std::vector<Mac> near = {mac(1), mac(2), mac(3), mac(4)};
  d.tick(1000, near);                              // seed -> {1,2,3}
  TEST_ASSERT_TRUE(d.current[1] == mac(2));

  std::vector<Mac> churned = {mac(1), mac(3), mac(4)};   // shown mac(2) leaves, still >=3
  d.tick(1050, churned);
  TEST_ASSERT_FALSE(d.current[1] == mac(2));       // replacement branch moved it off
  assertDistinctPresent(d, churned);

  std::vector<Mac> grown = {mac(1), mac(3), mac(4), mac(5), mac(6)};   // new lamps mid-walk
  std::set<Mac> shown;
  for (uint32_t t = 1100; t <= 1100 + 4 * kStaggerPeriodMs; t += 200) {
    d.tick(t, grown);
    assertDistinctPresent(d, grown);
    for (int i = 0; i < 3; ++i) shown.insert(d.current[i]);
  }
  TEST_ASSERT_TRUE(shown.count(mac(5)) == 1);      // fresh lamps reached by the walk
  TEST_ASSERT_TRUE(shown.count(mac(6)) == 1);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_zero_nearby_all_idle);
  RUN_TEST(test_one_nearby_all_same);
  RUN_TEST(test_two_nearby_pair_plus_idle);
  RUN_TEST(test_three_nearby_distinct_one_each);
  RUN_TEST(test_entry_seeds_staggered_deadlines);
  RUN_TEST(test_walk_distinct_and_staggered);
  RUN_TEST(test_dropout_replaced_stays_distinct);
  RUN_TEST(test_churn_keeps_trio_distinct);
  return UNITY_END();
}
