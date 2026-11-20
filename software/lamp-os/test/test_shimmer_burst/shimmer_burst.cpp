// Native pin for the flicker burst against the real ShimmerExpression: a
// transient burst completes so the transient GC reaps it, only its sparked
// cells ever paint, and a configured auto-triggering flicker carrying the same
// keys stays continuous.

#include <unity.h>

#include <vector>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/expressions/expression.cpp"
#include "../../src/expressions/shimmer/shimmer_expression.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"

using namespace lamp;

namespace lamp {
OverrideAggregate overrides;
void ExpressionManager::onExpressionFired(Expression*) {}
}  // namespace lamp

namespace {

constexpr uint8_t kPixels = 24;
constexpr uint32_t kStars = 5;
constexpr uint32_t kBurstMs = 400;
constexpr uint32_t kFrameMs = 16;
const Color kAnchor(120, 60, 0, 0);

struct Surfaces {
  FrameBuffer shade;
  FrameBuffer base;
  BehaviorContext ctx;
  Surfaces() {
    for (FrameBuffer* fb : {&shade, &base}) {
      fb->pixelCount = kPixels;
      fb->buffer.assign(kPixels, kAnchor);
    }
    ctx.expressionFrameBuffers = {&shade, &base};
  }
};

// The anchor carries no blue, and Twinkle's warmth ramp never adds any, so
// blue on the surface can only come from a tint.
const Color kBlueTint(0, 0, 255, 0);

void burstOn(ShimmerExpression& e, Surfaces& s, bool transient,
             std::vector<Color> colors = {}, uint32_t burstMs = kBurstMs) {
  e.configure(colors, 60, 900, TARGET_SHADE);
  e.configureFromParameters(
      {{"fire", 0}, {"count", kStars}, {"durationMax", burstMs}, {"opacity", 100}});
  e.isTransient = transient;
  e.autoTriggerEnabled = !transient;
  e.setBehaviorContext(&s.ctx);
}

// One compositor frame: the layer below repaints the anchor, then the
// expression runs its control and draw.
size_t frame(ShimmerExpression& e, Surfaces& s, uint32_t nowMs) {
  set_mock_millis(nowMs);
  s.shade.buffer.assign(kPixels, kAnchor);
  e.control();
  e.draw();
  size_t touched = 0;
  for (const Color& c : s.shade.buffer) touched += !(c == kAnchor);
  return touched;
}

}  // namespace

void setUp() { set_mock_millis(1000); }
void tearDown() {}

void test_a_transient_burst_completes_for_the_gc() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/true);
  TEST_ASSERT_TRUE(e.trigger());
  size_t peak = 0;
  uint32_t now = 1000;
  for (; now < 1000 + kBurstMs; now += kFrameMs) {
    TEST_ASSERT_FALSE(e.isAnimationComplete());
    const size_t touched = frame(e, s, now);
    peak = touched > peak ? touched : peak;
  }
  for (int i = 0; i < 3; ++i) frame(e, s, now += kFrameMs);
  TEST_ASSERT_TRUE(peak > 0);
  // gcTransients reaps on isAnimationComplete(); the 180 s cap is never reached.
  TEST_ASSERT_TRUE(e.isAnimationComplete());
  TEST_ASSERT_TRUE(now - 1000 < 180000);
}

void test_only_sparked_cells_ever_paint() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/true);
  e.trigger();
  for (uint32_t now = 1000; now < 1000 + kBurstMs + 4 * kFrameMs; now += kFrameMs) {
    TEST_ASSERT_TRUE(frame(e, s, now) <= kStars);
  }
}

void test_a_configured_flicker_with_burst_keys_stays_continuous() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/false);
  e.trigger();
  size_t widest = 0;
  for (uint32_t now = 1000; now < 1000 + 5 * kBurstMs; now += kFrameMs) {
    const size_t touched = frame(e, s, now);
    widest = touched > widest ? touched : widest;
  }
  TEST_ASSERT_FALSE(e.isAnimationComplete());
  TEST_ASSERT_TRUE(e.animationState != STOPPED);
  // Continuous Twinkle paints every cell of its zone.
  TEST_ASSERT_EQUAL_UINT32(kPixels, widest);
}

// The most blue and the most white any pixel reached over the run. The anchor
// carries neither, so either can only come from a colour other than it.
struct Reach {
  uint8_t blue = 0;
  uint8_t white = 0;
};

Reach reach(ShimmerExpression& e, Surfaces& s, uint32_t spanMs) {
  e.trigger();
  Reach r;
  for (uint32_t now = 1000; now < 1000 + spanMs; now += kFrameMs) {
    frame(e, s, now);
    for (const Color& c : s.shade.buffer) {
      r.blue = c.b > r.blue ? c.b : r.blue;
      r.white = c.w > r.white ? c.w : r.white;
    }
  }
  return r;
}

void test_a_burst_paints_its_tint() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/true, {kBlueTint});
  TEST_ASSERT_TRUE(reach(e, s, kBurstMs).blue > 0);
}

void test_an_untinted_burst_paints_the_colour_under_it() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/true);
  const Reach r = reach(e, s, kBurstMs);
  TEST_ASSERT_EQUAL_UINT8(0, r.blue);
  TEST_ASSERT_EQUAL_UINT8(0, r.white);
}

void test_a_configured_flicker_ignores_a_tint() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/false, {kBlueTint});
  TEST_ASSERT_EQUAL_UINT8(0, reach(e, s, 5 * kBurstMs).blue);
}

// A configured flicker the user disabled has its auto-trigger off like a
// transient, yet a Test fire of it still runs continuously.
void test_a_disabled_configured_flicker_never_bursts() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/false);
  e.autoTriggerEnabled = false;
  e.trigger();
  for (uint32_t now = 1000; now < 1000 + 3 * kBurstMs; now += kFrameMs) frame(e, s, now);
  TEST_ASSERT_FALSE(e.isAnimationComplete());
}

// Runs one burst and records each frame's lit count until the instance
// completes; `lit` marks every cell that ever painted.
std::vector<size_t> runBurst(ShimmerExpression& e, Surfaces& s, uint32_t startMs,
                             std::vector<bool>* lit = nullptr) {
  set_mock_millis(startMs);
  TEST_ASSERT_TRUE(e.trigger());
  std::vector<size_t> counts;
  for (uint32_t now = startMs; !e.isAnimationComplete(); now += kFrameMs) {
    counts.push_back(frame(e, s, now));
    if (lit) {
      for (uint8_t i = 0; i < kPixels; ++i) {
        if (!(s.shade.buffer[i] == kAnchor)) (*lit)[i] = true;
      }
    }
    TEST_ASSERT_TRUE(now - startMs < 4 * kBurstMs);
  }
  return counts;
}

void test_a_burst_lights_exactly_count_stars() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/true);
  const std::vector<size_t> counts = runBurst(e, s, 1000);
  size_t peak = 0;
  for (size_t c : counts) peak = c > peak ? c : peak;
  TEST_ASSERT_EQUAL_UINT32(kStars, peak);
}

// Every star has fallen back under the paint threshold before the burst
// completes, so completion removes nothing still lit.
void test_a_burst_falls_dark_before_it_completes() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/true);
  const std::vector<size_t> counts = runBurst(e, s, 1000);
  TEST_ASSERT_TRUE(counts.size() > 4);
  TEST_ASSERT_EQUAL_UINT32(0, counts[counts.size() - 2]);
  TEST_ASSERT_EQUAL_UINT32(0, counts.back());
}

// Over many triggers the stars spread across the zone: each half of it holds
// about its share, so selection favours no end.
void test_stars_spread_over_the_whole_zone() {
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/true);
  uint32_t front = 0, back = 0;
  uint32_t start = 1000;
  for (int burst = 0; burst < 300; ++burst) {
    std::vector<bool> lit(kPixels, false);
    runBurst(e, s, start, &lit);
    start += 4 * kBurstMs;
    for (uint8_t i = 0; i < kPixels; ++i) {
      if (lit[i]) (i < kPixels / 2 ? front : back) += 1;
    }
  }
  const float share = static_cast<float>(back) / static_cast<float>(front + back);
  TEST_ASSERT_TRUE_MESSAGE(share > 0.42f && share < 0.58f, "stars cluster at one end");
}

// A burst past the 2 s swell runs its whole length: every star stays lit
// through the hold, all are dark by the end, and it completes at its length.
void test_a_long_burst_holds_its_stars_and_completes_at_its_length() {
  constexpr uint32_t kLongMs = 5000;
  Surfaces s;
  ShimmerExpression e(&s.shade);
  burstOn(e, s, /*transient=*/true, {}, kLongMs);
  TEST_ASSERT_TRUE(e.trigger());
  uint32_t now = 1000;
  size_t fewestHeld = kStars;
  size_t lastLit = 0;
  for (; now - 1000 < kLongMs; now += kFrameMs) {
    TEST_ASSERT_FALSE_MESSAGE(e.isAnimationComplete(), "long burst ended early");
    lastLit = frame(e, s, now);
    const uint32_t elapsed = now - 1000;
    if (elapsed >= 1000 && elapsed < kLongMs - 1000) {
      fewestHeld = lastLit < fewestHeld ? lastLit : fewestHeld;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(kStars, fewestHeld);
  TEST_ASSERT_EQUAL_UINT32(0, lastLit);
  frame(e, s, now);
  frame(e, s, now += kFrameMs);
  TEST_ASSERT_TRUE(e.isAnimationComplete());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_transient_burst_completes_for_the_gc);
  RUN_TEST(test_only_sparked_cells_ever_paint);
  RUN_TEST(test_a_configured_flicker_with_burst_keys_stays_continuous);
  RUN_TEST(test_a_burst_paints_its_tint);
  RUN_TEST(test_an_untinted_burst_paints_the_colour_under_it);
  RUN_TEST(test_a_configured_flicker_ignores_a_tint);
  RUN_TEST(test_a_disabled_configured_flicker_never_bursts);
  RUN_TEST(test_a_burst_lights_exactly_count_stars);
  RUN_TEST(test_a_burst_falls_dark_before_it_completes);
  RUN_TEST(test_stars_spread_over_the_whole_zone);
  RUN_TEST(test_a_long_burst_holds_its_stars_and_completes_at_its_length);
  return UNITY_END();
}
