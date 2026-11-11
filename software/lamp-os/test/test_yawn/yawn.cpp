// Native pin for the yawn expression: the level curve (exactly the scene at
// both ends, continuous, reaching the requested depth inside the requested
// duration) and the real YawnExpression on stub surfaces (scene untouched
// outside the yawn, base trailing the shade, completion for the GC).

#include <unity.h>

#include <cmath>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/expressions/expression.cpp"
#include "../../src/expressions/expression_registry.cpp"
#include "../../src/expressions/yawn/yawn_expression.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"

using namespace lamp;

namespace lamp {
OverrideAggregate overrides;
void ExpressionManager::onExpressionFired(Expression*) {}
}  // namespace lamp

namespace {

YawnShape defaultShape() {
  return {.durationMs = 4500, .inhaleMs = 600, .holdMs = 500, .recoverMs = 1800,
          .lift = 0.08f, .depth = 0.35f};
}

// No inhale, a long recover and a hold that together overrun the fixed-phase
// budget, so the phases shrink to fit.
YawnShape overrunShape() {
  return {.durationMs = 3000, .inhaleMs = 0, .holdMs = 1500, .recoverMs = 2500,
          .lift = 0.2f, .depth = 0.12f};
}

struct Sweep {
  float lo = 2.0f;
  float hi = 0.0f;
  float maxStep = 0.0f;
  uint32_t lastOffScene = 0;
};

Sweep sweep(const YawnShape& s) {
  Sweep r;
  float prev = yawnLevel(s, 0);
  for (uint32_t t = 0; t <= s.durationMs + 50; ++t) {
    const float v = yawnLevel(s, t);
    r.lo = std::min(r.lo, v);
    r.hi = std::max(r.hi, v);
    r.maxStep = std::max(r.maxStep, std::fabs(v - prev));
    if (v != 1.0f) r.lastOffScene = t;
    prev = v;
  }
  return r;
}

void assertCurve(const YawnShape& s, float wantPeak) {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, yawnLevel(s, 0));
  TEST_ASSERT_TRUE(yawnLevel(s, 0) == 1.0f);
  TEST_ASSERT_TRUE(yawnLevel(s, s.durationMs) == 1.0f);
  TEST_ASSERT_TRUE(yawnLevel(s, s.durationMs + 1000) == 1.0f);
  const Sweep r = sweep(s);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, s.depth, r.lo);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, wantPeak, r.hi);
  // A 1 ms step of the steepest phase moves well under 1% of the scene.
  TEST_ASSERT_TRUE(r.maxStep < 0.005f);
  // Still off the scene in the last millisecond, back on it at durationMs.
  TEST_ASSERT_EQUAL_UINT32(s.durationMs - 1, r.lastOffScene);
}

// Frame-level fixture: a lit scene on both surfaces, redrawn each frame as
// the configurator would before the yawn composites over it.
constexpr uint8_t kPixels = 12;
constexpr uint32_t kFrameMs = 16;
constexpr uint32_t kStartMs = 5000;
const Color kScene(180, 120, 60, 40);

struct Surfaces {
  FrameBuffer shade;
  FrameBuffer base;
  BehaviorContext ctx;
  Surfaces() {
    for (FrameBuffer* fb : {&shade, &base}) {
      fb->pixelCount = kPixels;
      fb->buffer.assign(kPixels, kScene);
    }
    ctx.expressionFrameBuffers = {&shade, &base};
  }
};

void yawnOn(YawnExpression& e, Surfaces& s, ExpressionParams params) {
  ExpressionRegistry reg;
  reg.applyDefaults(kYawnDescriptorData, params, kPixels);
  e.joinSurfaceAbove(&s.shade);
  e.configure({}, 60, 900, TARGET_BOTH);
  e.configureFromParameters(params);
  e.isTransient = true;
  e.autoTriggerEnabled = false;
  e.setBehaviorContext(&s.ctx);
}

void frame(YawnExpression& e, Surfaces& s, uint32_t nowMs) {
  set_mock_millis(nowMs);
  s.shade.buffer.assign(kPixels, kScene);
  s.base.buffer.assign(kPixels, kScene);
  e.control();
  e.draw();
}

bool onScene(const FrameBuffer& fb) {
  for (const Color& c : fb.buffer) {
    if (!(c == kScene)) return false;
  }
  return true;
}

}  // namespace

void setUp() { set_mock_millis(kStartMs); }
void tearDown() {}

void test_default_curve_starts_and_ends_on_the_scene() {
  assertCurve(defaultShape(), 1.08f);
}

void test_overrun_phases_still_land_on_the_scene() {
  assertCurve(overrunShape(), 1.0f);
}

void test_hold_sits_at_the_depth_before_the_recover() {
  const YawnShape s = defaultShape();
  const uint32_t holdMid = s.durationMs - s.recoverMs - s.holdMs / 2;
  TEST_ASSERT_TRUE(yawnLevel(s, holdMid) == s.depth);
  TEST_ASSERT_TRUE(yawnLevel(s, s.durationMs - s.recoverMs + 100) > s.depth);
}

void test_scene_gains_leave_every_channel_exact() {
  const YawnGains g = yawnGains(1.0f, 0.0f);
  TEST_ASSERT_EQUAL_UINT16(256, g.r);
  TEST_ASSERT_EQUAL_UINT16(256, g.g);
  TEST_ASSERT_EQUAL_UINT16(256, g.b);
  TEST_ASSERT_EQUAL_UINT16(256, g.w);
  for (int v = 0; v <= 255; v += 17) {
    const Color c(v, 255 - v, (v * 7) & 0xFF, (v * 3) & 0xFF);
    TEST_ASSERT_TRUE(yawnPixel(c, g) == c);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, yawnWarmWeight(1.0f, 0.35f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, yawnWarmWeight(0.35f, 0.35f));
}

void test_warmth_cuts_blue_more_than_green_and_spares_red() {
  const YawnGains g = yawnGains(0.5f, 1.0f);
  TEST_ASSERT_EQUAL_UINT16(128, g.r);
  TEST_ASSERT_TRUE(g.b < g.g);
  TEST_ASSERT_TRUE(g.g < g.r);
}

void test_yawn_leaves_the_scene_exact_outside_its_run_and_completes() {
  Surfaces s;
  YawnExpression e(&s.base);
  yawnOn(e, s, {{kYawnWarmthKey, 0}});
  TEST_ASSERT_TRUE(e.trigger());
  frame(e, s, kStartMs);
  TEST_ASSERT_TRUE(onScene(s.shade));
  TEST_ASSERT_TRUE(onScene(s.base));

  const uint32_t runMs = 4500 + 200;
  uint8_t minRed = 255;
  uint32_t now = kStartMs;
  for (; now < kStartMs + runMs; now += kFrameMs) {
    TEST_ASSERT_FALSE(e.isAnimationComplete());
    frame(e, s, now);
    minRed = std::min(minRed, s.shade.buffer[0].r);
  }
  // Red is never warm-cut, so its floor is the depth: 35% of 180.
  TEST_ASSERT_UINT8_WITHIN(1, 63, minRed);
  for (int i = 0; i < 3; ++i) frame(e, s, now += kFrameMs);
  TEST_ASSERT_TRUE(e.isAnimationComplete());
  TEST_ASSERT_TRUE(onScene(s.shade));
  TEST_ASSERT_TRUE(onScene(s.base));
}

void test_base_trails_the_shade_by_the_lag() {
  Surfaces s;
  YawnExpression e(&s.base);
  yawnOn(e, s, {{kYawnLagKey, 400}});
  e.trigger();
  uint32_t shadeLeft = 0;
  uint32_t baseLeft = 0;
  for (uint32_t now = kStartMs; now < kStartMs + 2000; now += kFrameMs) {
    frame(e, s, now);
    if (!shadeLeft && !onScene(s.shade)) shadeLeft = now;
    if (!baseLeft && !onScene(s.base)) baseLeft = now;
  }
  TEST_ASSERT_TRUE(shadeLeft > 0 && baseLeft > 0);
  TEST_ASSERT_UINT32_WITHIN(kFrameMs, 400, baseLeft - shadeLeft);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_curve_starts_and_ends_on_the_scene);
  RUN_TEST(test_overrun_phases_still_land_on_the_scene);
  RUN_TEST(test_hold_sits_at_the_depth_before_the_recover);
  RUN_TEST(test_scene_gains_leave_every_channel_exact);
  RUN_TEST(test_warmth_cuts_blue_more_than_green_and_spares_red);
  RUN_TEST(test_yawn_leaves_the_scene_exact_outside_its_run_and_completes);
  RUN_TEST(test_base_trails_the_shade_by_the_lag);
  return UNITY_END();
}
