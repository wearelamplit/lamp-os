#include <unity.h>

#include "osc/osc_freshness.hpp"

using wisp::OscFreshness;

void setUp() {}
void tearDown() {}

void test_first_palette_is_live_edge() {
  OscFreshness f;
  TEST_ASSERT_FALSE(f.live());
  TEST_ASSERT_TRUE(f.onPalette(100));
  TEST_ASSERT_TRUE(f.live());
  TEST_ASSERT_FALSE(f.onPalette(200));
}

void test_stale_after_gap_once() {
  OscFreshness f;
  f.onPalette(1000);
  TEST_ASSERT_FALSE(f.tick(1000 + OscFreshness::kStaleMs - 1));
  TEST_ASSERT_TRUE(f.live());
  TEST_ASSERT_TRUE(f.tick(1000 + OscFreshness::kStaleMs));
  TEST_ASSERT_FALSE(f.live());
  TEST_ASSERT_FALSE(f.tick(1000 + 2 * OscFreshness::kStaleMs));
}

// A palette after a gap resumes on its own: the stale -> live edge fires again.
void test_resumes_after_gap() {
  OscFreshness f;
  f.onPalette(0);
  f.tick(OscFreshness::kStaleMs);
  TEST_ASSERT_TRUE(f.onPalette(OscFreshness::kStaleMs + 5));
  TEST_ASSERT_TRUE(f.live());
}

void test_steady_stream_never_stales() {
  OscFreshness f;
  for (uint32_t t = 0; t < 5 * OscFreshness::kStaleMs; t += 500) {
    f.onPalette(t);
    TEST_ASSERT_FALSE(f.tick(t + 499));
  }
  TEST_ASSERT_TRUE(f.live());
}

void test_millis_wrap() {
  OscFreshness f;
  f.onPalette(0xFFFFFF00u);
  TEST_ASSERT_FALSE(f.tick(0x00000100u));
  TEST_ASSERT_TRUE(f.tick(0xFFFFFF00u + OscFreshness::kStaleMs));
}

void test_reset_drops_live_without_edge() {
  OscFreshness f;
  f.onPalette(0);
  f.reset();
  TEST_ASSERT_FALSE(f.live());
  TEST_ASSERT_FALSE(f.tick(OscFreshness::kStaleMs * 2));
  TEST_ASSERT_TRUE(f.onPalette(1));
}

// Straylight restates a still palette every 10 s over UDP. Arrival jitter
// and one lost restate must never read as stale.
void test_ten_second_restate_stays_live() {
  OscFreshness f;
  uint32_t t = 0;
  for (int i = 0; i < 12; ++i) {
    f.onPalette(t);
    TEST_ASSERT_FALSE(f.tick(t + 10050));
    t += 10050;
  }
  // One restate dropped: the next lands a full cycle late.
  TEST_ASSERT_FALSE(f.tick(t + 10100));
  f.onPalette(t + 10100);
  TEST_ASSERT_TRUE(f.live());
}

void test_blank_palette_detected() {
  const wisp::PaletteColor zeros[2] = {};
  TEST_ASSERT_TRUE(wisp::isBlankPalette(zeros, 2));
  const wisp::PaletteColor negative[1] = {{-1.0f, 0.0f, -0.5f, 0.0f}};
  TEST_ASSERT_TRUE(wisp::isBlankPalette(negative, 1));
  const wisp::PaletteColor oneLit[2] = {{}, {0, 0.01f, 0, 0}};
  TEST_ASSERT_FALSE(wisp::isBlankPalette(oneLit, 2));
}

// White drives the lamps' warm-white LED, so a palette lit only on w paints.
void test_white_only_palette_is_not_blank() {
  const wisp::PaletteColor whiteOnly[3] = {{}, {0, 0, 0, 1.0f}, {}};
  TEST_ASSERT_FALSE(wisp::isBlankPalette(whiteOnly, 3));
}

// Over UDP the palette datagram can drop on every restate while the rest of
// the feed lands: feed traffic keeps a held palette live.
void test_feed_keeps_held_palette_live() {
  OscFreshness f;
  f.onPalette(0);
  for (uint32_t t = 10000; t <= 120000; t += 10000) {
    f.onFeed(t);
    TEST_ASSERT_FALSE(f.tick(t + 9999));
  }
  TEST_ASSERT_TRUE(f.live());
}

// Feed traffic never makes a stream live: a palette must arrive first.
void test_feed_does_not_make_stale_live() {
  OscFreshness f;
  f.onFeed(100);
  TEST_ASSERT_FALSE(f.live());
  f.onPalette(0);
  TEST_ASSERT_TRUE(f.tick(OscFreshness::kStaleMs));
  f.onFeed(OscFreshness::kStaleMs + 1);
  TEST_ASSERT_FALSE(f.live());
  TEST_ASSERT_TRUE(f.onPalette(OscFreshness::kStaleMs + 2));
}

// A palette differing only in white is a change; the same palette and role
// again is not; a role change is.
void test_memo_sees_white_and_role_changes() {
  wisp::OscPaletteMemo memo;
  const wisp::PaletteColor warm[2] = {{1, 0, 0, 0}, {0, 0, 1, 0.5f}};
  const wisp::PaletteColor warmer[2] = {{1, 0, 0, 0}, {0, 0, 1, 1.0f}};
  TEST_ASSERT_TRUE(memo.hold(warm, 2, wisp::OscRole::Primary));
  TEST_ASSERT_FALSE(memo.hold(warm, 2, wisp::OscRole::Primary));
  TEST_ASSERT_TRUE(memo.hold(warmer, 2, wisp::OscRole::Primary));
  TEST_ASSERT_TRUE(memo.hold(warmer, 2, wisp::OscRole::Fill));
  TEST_ASSERT_TRUE(memo.hold(warmer, 1, wisp::OscRole::Fill));
  memo.clear();
  TEST_ASSERT_TRUE(memo.hold(warmer, 1, wisp::OscRole::Fill));
}

// More colours than the memo holds are refused and leave the held palette.
void test_memo_refuses_an_oversized_palette() {
  wisp::OscPaletteMemo memo;
  const wisp::PaletteColor one[1] = {{0, 1, 0, 0}};
  TEST_ASSERT_TRUE(memo.hold(one, 1, wisp::OscRole::Primary));
  wisp::PaletteColor many[wisp::kOscPaletteMaxColors + 1] = {};
  TEST_ASSERT_FALSE(memo.hold(many, wisp::kOscPaletteMaxColors + 1,
                              wisp::OscRole::Primary));
  TEST_ASSERT_FALSE(memo.hold(one, 1, wisp::OscRole::Primary));
}

// A zero window (Maestro) holds the last palette however long the sender
// stays quiet, and reset still drops it.
void test_zero_window_never_stales() {
  OscFreshness f;
  f.setStaleMs(0);
  f.onPalette(1000);
  TEST_ASSERT_FALSE(f.tick(1000 + 3600000u));
  TEST_ASSERT_FALSE(f.tick(1000 + 0x7FFFFFFFu));
  TEST_ASSERT_TRUE(f.live());
  f.reset();
  TEST_ASSERT_FALSE(f.live());
}

// The window set is the one in force, wider or narrower than the default.
void test_window_is_settable() {
  OscFreshness f;
  f.setStaleMs(5000);
  f.onPalette(0);
  TEST_ASSERT_FALSE(f.tick(4999));
  TEST_ASSERT_TRUE(f.tick(5000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_palette_is_live_edge);
  RUN_TEST(test_stale_after_gap_once);
  RUN_TEST(test_resumes_after_gap);
  RUN_TEST(test_steady_stream_never_stales);
  RUN_TEST(test_millis_wrap);
  RUN_TEST(test_reset_drops_live_without_edge);
  RUN_TEST(test_ten_second_restate_stays_live);
  RUN_TEST(test_blank_palette_detected);
  RUN_TEST(test_white_only_palette_is_not_blank);
  RUN_TEST(test_memo_sees_white_and_role_changes);
  RUN_TEST(test_memo_refuses_an_oversized_palette);
  RUN_TEST(test_feed_keeps_held_palette_live);
  RUN_TEST(test_feed_does_not_make_stale_live);
  RUN_TEST(test_zero_window_never_stales);
  RUN_TEST(test_window_is_settable);
  return UNITY_END();
}
