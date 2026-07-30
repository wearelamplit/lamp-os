// Pins the [wispstate] control-plane meter: this-lamp adopt/release edge
// counting and the per-window reset that keeps a held paint from re-counting.

#include <unity.h>
#include <cstdint>

#include "components/network/mesh/wisp_state.hpp"  // -I src on native

using lamp::WispStateEdge;
using lamp::WispStateMeter;

void setUp(void) {}
void tearDown(void) {}

void test_recv_counts_every_frame() {
  WispStateMeter m;
  m.record(false);
  m.record(false);
  TEST_ASSERT_EQUAL_UINT32(2, m.recv());
}

void test_first_self_present_is_adopt_edge() {
  WispStateMeter m;
  TEST_ASSERT_TRUE(WispStateEdge::kAdopt == m.record(true));
  TEST_ASSERT_EQUAL_UINT32(1, m.adopts());
  TEST_ASSERT_EQUAL_UINT32(1, m.selfPresent());
}

void test_held_adopt_does_not_recount() {
  WispStateMeter m;
  m.record(true);
  TEST_ASSERT_TRUE(WispStateEdge::kNone == m.record(true));
  TEST_ASSERT_EQUAL_UINT32(1, m.adopts());
  TEST_ASSERT_EQUAL_UINT32(2, m.selfPresent());
}

void test_absence_after_adopt_is_release_edge() {
  WispStateMeter m;
  m.record(true);
  TEST_ASSERT_TRUE(WispStateEdge::kRelease == m.record(false));
  TEST_ASSERT_EQUAL_UINT32(1, m.releases());
}

void test_held_release_does_not_recount() {
  WispStateMeter m;
  m.record(true);
  m.record(false);
  TEST_ASSERT_TRUE(WispStateEdge::kNone == m.record(false));
  TEST_ASSERT_EQUAL_UINT32(1, m.releases());
}

void test_self_present_counts_only_present_frames() {
  WispStateMeter m;
  m.record(true);
  m.record(false);
  m.record(true);
  TEST_ASSERT_EQUAL_UINT32(2, m.selfPresent());
}

void test_window_reset_zeros_counters_keeps_adopt_state() {
  WispStateMeter m;
  m.record(true);  // adopt
  m.record(true);  // held
  m.resetWindow();
  TEST_ASSERT_EQUAL_UINT32(0, m.recv());
  TEST_ASSERT_EQUAL_UINT32(0, m.adopts());
  TEST_ASSERT_EQUAL_UINT32(0, m.selfPresent());
  // Held adopt persists across the window boundary: no re-adopt.
  TEST_ASSERT_TRUE(WispStateEdge::kNone == m.record(true));
  TEST_ASSERT_EQUAL_UINT32(0, m.adopts());
  // A drop after the reset still fires a release edge.
  TEST_ASSERT_TRUE(WispStateEdge::kRelease == m.record(false));
  TEST_ASSERT_EQUAL_UINT32(1, m.releases());
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_recv_counts_every_frame);
  RUN_TEST(test_first_self_present_is_adopt_edge);
  RUN_TEST(test_held_adopt_does_not_recount);
  RUN_TEST(test_absence_after_adopt_is_release_edge);
  RUN_TEST(test_held_release_does_not_recount);
  RUN_TEST(test_self_present_counts_only_present_frames);
  RUN_TEST(test_window_reset_zeros_counters_keeps_adopt_state);
  return UNITY_END();
}
