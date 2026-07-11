// Native tests for the segment-aware snafu DotsBehavior.
//
// Adafruit_NeoPixel + esp_system are stubbed under test/native_stubs/. Pins:
//   1. Per-segment palette, no cross-segment bleed — each FrameBuffer segment's
//      slice draws only from its own palette.
//   2. Per-cycle build — the sampled scene is rebuilt in control(), not
//      re-randomized every draw() frame (frame-to-frame stable without a
//      control() tick).
//   3. Single-color segments render static (control() is a no-op).

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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_cross_segment_bleed);
  RUN_TEST(test_scene_sampled_per_cycle_not_per_frame);
  RUN_TEST(test_single_color_segment_is_static);
  return UNITY_END();
}
