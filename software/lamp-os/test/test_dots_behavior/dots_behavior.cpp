// Native tests for the segment-aware snafu DotsBehavior.
//
// Adafruit_NeoPixel + esp_system are stubbed under test/native_stubs/. Pins:
//   1. Per-segment palette, no cross-segment bleed — each FrameBuffer segment's
//      slice draws only from its own palette.
//   2. Per-cycle build — the sampled scene is rebuilt in control(), not
//      re-randomized every draw() frame (frame-to-frame stable without a
//      control() tick).
//   3. Single-color segments render static (control() is a no-op).
//   4. borrowColors({}, {}, ...) is a true no-op: scatter and output match an
//      unborrowed instance.
//   5. Borrowed palette maps by segment index (0 -> shade, else base) and
//      auto-reverts to each segment's own palette once durationFrames elapse.
//   6. Past the borrow window's halfway point, the rendered buffer melts from
//      the borrowed color toward the own-palette color, landing on neither
//      endpoint mid-melt.
//   7. Every kBorrowHoldFrames control() ticks during an active borrow,
//      perm_ re-scatters: the rendered pixel arrangement changes even
//      though the borrow stays active and short of the halfway melt.

#include <unity.h>

#include <cstdint>
#include <vector>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/lamps/snafu/dots_behavior.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"

namespace lamp {
// dots_behavior.cpp forward-declares this; the native suite doesn't link
// expression.cpp, so provide the never-overriding stub here.
bool isWispCurrentlyOverriding() { return false; }
}  // namespace lamp

using namespace lamp;
using lamp::snafu::DotsBehavior;

static Adafruit_NeoPixel neo0;
static Adafruit_NeoPixel neo1;

void setUp() {}
void tearDown() {}

// seg0 palette rides the R channel only, seg1 the B channel only, so any
// cross-bleed shows up as a nonzero channel that segment can't produce.
static ShadeSettings twoSegmentConfig() {
  ShadeSettings cfg;
  cfg.segments = {
    {"a", 3, {Color(0x10, 0, 0, 0), Color(0xF0, 0, 0, 0)}},
    {"b", 2, {Color(0, 0, 0x10, 0), Color(0, 0, 0xF0, 0)}},
  };
  return cfg;
}

static FrameBuffer makeFb() {
  FrameBuffer fb;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "a", 0, 3}, {&neo1, "b", 3, 2}});
  return fb;
}

void test_no_cross_segment_bleed() {
  ShadeSettings cfg = twoSegmentConfig();
  FrameBuffer fb = makeFb();
  DotsBehavior dots(&fb, cfg);

  dots.control();
  dots.draw();

  // seg0 (buffer[0..3)) is R-only: g/b/w stay zero.
  for (uint16_t i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].g);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].b);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].w);
  }
  // seg1 (buffer[3..5)) is B-only: r/g/w stay zero.
  for (uint16_t i = 3; i < 5; i++) {
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].r);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].g);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].w);
  }
}

// Once a cycle's crossfade settles, draw() writes the sampled scene verbatim —
// it does not re-shuffle per frame. Only a control() tick samples a new scene.
// Short frame window (2) so the crossfade completes quickly.
void test_scene_sampled_per_cycle_not_per_frame() {
  ShadeSettings cfg = twoSegmentConfig();
  FrameBuffer fb = makeFb();
  DotsBehavior dots(&fb, cfg, /*inFrames=*/2);

  dots.control();               // samples one scene for this cycle
  for (int i = 0; i < 5; i++) dots.draw();  // finish the crossfade → steady
  std::vector<Color> snap = fb.buffer;

  dots.draw();                  // no control() → same sampled scene
  TEST_ASSERT_TRUE(snap == fb.buffer);
  dots.draw();
  TEST_ASSERT_TRUE(snap == fb.buffer);
}

void test_single_color_segment_is_static() {
  ShadeSettings cfg;
  cfg.segments = {{"a", 3, {Color(0xAA, 0xBB, 0xCC, 0xDD)}}};
  FrameBuffer fb;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "a", 0, 3}});
  DotsBehavior dots(&fb, cfg);

  dots.control();  // anyMultiColor() false → no scene change
  dots.draw();
  for (uint16_t i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(Color(0xAA, 0xBB, 0xCC, 0xDD) == fb.buffer[i]);
  }
}

// esp_random() is a fixed stub value, so two freshly constructed DotsBehavior
// instances with identical config scatter identically. An empty-stops
// borrowColors() call must be a true no-op: it must not touch rng_ (which
// would desync the scatter) or flip borrowActive_.
void test_borrow_empty_stops_is_noop() {
  ShadeSettings cfg = twoSegmentConfig();

  FrameBuffer fbBaseline = makeFb();
  DotsBehavior baseline(&fbBaseline, cfg);
  baseline.control();
  baseline.draw();

  FrameBuffer fbBorrowed = makeFb();
  DotsBehavior borrowed(&fbBorrowed, cfg);
  borrowed.borrowColors({}, {}, 100);
  borrowed.control();
  borrowed.draw();

  TEST_ASSERT_TRUE(fbBaseline.buffer == fbBorrowed.buffer);
}

// index 0 ("a") borrows the peer shade stop, index 1+ ("b") borrows the peer
// base stop. After durationFrames control() ticks the borrow auto-reverts to
// each segment's own palette. Both peer palettes here are single-stop
// (solid), so buildSceneWith jitters brightness/saturation per pixel; assert
// bounds instead of an exact color. peerBase is W-only (r=g=b=0), so its
// zero channels have no mean to bleed toward and stay exactly 0; peerShade's
// r/b start at 0 with g nonzero, so the saturation pull can bleed them a
// little off zero.
void test_borrow_overrides_by_index_then_reverts() {
  ShadeSettings cfg = twoSegmentConfig();
  FrameBuffer fb = makeFb();
  DotsBehavior dots(&fb, cfg);

  const std::vector<Color> peerBase  = {Color(0, 0, 0, 0x77)};
  const std::vector<Color> peerShade = {Color(0, 0x77, 0, 0)};
  dots.borrowColors(peerBase, peerShade, /*durationFrames=*/3);
  dots.draw();

  for (uint16_t i = 0; i < 3; i++) {
    TEST_ASSERT_GREATER_THAN_UINT8(0, fb.buffer[i].g);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(0x77, fb.buffer[i].g);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(16, fb.buffer[i].r);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(16, fb.buffer[i].b);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].w);
  }
  for (uint16_t i = 3; i < 5; i++) {
    TEST_ASSERT_GREATER_THAN_UINT8(0, fb.buffer[i].w);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(0x77, fb.buffer[i].w);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].r);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].g);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].b);
  }

  dots.control();  // elapsed 1/3
  dots.control();  // elapsed 2/3
  dots.control();  // elapsed 3/3 -> reverts
  dots.draw();

  for (uint16_t i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].g);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].b);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].w);
  }
  for (uint16_t i = 3; i < 5; i++) {
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].r);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].g);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].w);
  }
}

// Past the halfway point of the borrow window, draw() blends the borrowed
// color toward ownMelt_ (the own-palette equivalent). duration=10 keeps the
// window short; elapsed=8 lands well inside the second half without
// reaching the auto-revert at elapsed>=10.
void test_borrow_melts_toward_own_after_halfway() {
  ShadeSettings cfg;
  cfg.segments = {{"a", 3, {Color(0xAA, 0, 0, 0)}}};
  FrameBuffer fb;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "a", 0, 3}});
  DotsBehavior dots(&fb, cfg);

  const Color peerShade(0, 0xCC, 0, 0);
  dots.borrowColors({}, {peerShade}, /*durationFrames=*/10);
  for (int i = 0; i < 8; i++) dots.control();  // elapsed = 8, past halfway (5)
  dots.draw();

  for (uint16_t i = 0; i < 3; i++) {
    TEST_ASSERT_GREATER_THAN_UINT8(0, fb.buffer[i].r);
    TEST_ASSERT_LESS_THAN_UINT8(0xAA, fb.buffer[i].r);
    TEST_ASSERT_GREATER_THAN_UINT8(0, fb.buffer[i].g);
    TEST_ASSERT_LESS_THAN_UINT8(0xCC, fb.buffer[i].g);
  }
}

// A single-stop borrow palette is solid; buildSceneWith jitters each pixel's
// brightness/saturation so borrowed peers don't render as one flat color.
void test_borrow_solid_color_gets_jittered() {
  ShadeSettings cfg;
  cfg.segments = {{"a", 8, {Color(0x40, 0x80, 0x40, 0)}}};
  FrameBuffer fb;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "a", 0, 8}});
  DotsBehavior dots(&fb, cfg);

  dots.borrowColors({}, {Color(0x40, 0x80, 0x40, 0)}, /*durationFrames=*/100);
  dots.draw();

  bool anyDifferent = false;
  for (uint16_t i = 1; i < 8; i++) {
    if (!(fb.buffer[i] == fb.buffer[0])) { anyDifferent = true; break; }
  }
  TEST_ASSERT_TRUE(anyDifferent);
}

// Every kBorrowHoldFrames control() ticks during an active borrow, perm_
// re-scatters. duration comfortably outlasts a hold+settle cycle so the
// rescatter fires before auto-revert; multi-stop peer palettes give the
// gradient distinct per-pixel values so a re-scatter is visible in the
// rendered buffer.
void test_borrow_periodic_rescatter_changes_arrangement() {
  ShadeSettings cfg = twoSegmentConfig();
  FrameBuffer fb = makeFb();
  DotsBehavior dots(&fb, cfg);

  const std::vector<Color> peerBase  = {Color(0, 0, 0, 0x20), Color(0, 0, 0, 0xE0)};
  const std::vector<Color> peerShade = {Color(0, 0x20, 0, 0), Color(0, 0xE0, 0, 0)};
  const uint32_t duration = DotsBehavior::kBorrowHoldFrames + DotsBehavior::kBorrowSceneFrames + 100;
  dots.borrowColors(peerBase, peerShade, duration);
  dots.draw();  // sceneChange_ false right after borrowColors -> renders cur_ verbatim
  const std::vector<Color> beforeRescatter = fb.buffer;

  for (uint32_t i = 0; i < DotsBehavior::kBorrowHoldFrames; i++) dots.control();
  for (uint32_t i = 0; i < DotsBehavior::kBorrowSceneFrames; i++) dots.draw();  // settle the post-rescatter crossfade

  TEST_ASSERT_FALSE(beforeRescatter == fb.buffer);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_cross_segment_bleed);
  RUN_TEST(test_scene_sampled_per_cycle_not_per_frame);
  RUN_TEST(test_single_color_segment_is_static);
  RUN_TEST(test_borrow_empty_stops_is_noop);
  RUN_TEST(test_borrow_overrides_by_index_then_reverts);
  RUN_TEST(test_borrow_melts_toward_own_after_halfway);
  RUN_TEST(test_borrow_solid_color_gets_jittered);
  RUN_TEST(test_borrow_periodic_rescatter_changes_arrangement);
  return UNITY_END();
}
