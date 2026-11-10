// Shifty's zone over a base that moves every frame: both fades and the hold
// blend against the live pixel the layers below drew that frame, so the zone
// follows a rotating base and the release shows that base with no pop.

#include <unity.h>

#include <cstdio>
#include <vector>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/expressions/expression.cpp"
#include "../../src/expressions/shifty/shifty_expression.cpp"

namespace lamp {
OverrideAggregate overrides;
void ExpressionManager::onExpressionFired(Expression*) {}
bool ExpressionManager::triggerExpression(const std::string&) { return false; }
}  // namespace lamp

using namespace lamp;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint8_t kPx = 20;
constexpr uint32_t kFadeMs = 2000;
constexpr uint32_t kHoldMs = 1000;
constexpr uint32_t kStepMs = 50;
const Color kShifted(200, 10, 30, 90);

// A base that moves one pixel per frame, every channel varying.
Color base(size_t i, uint32_t frame) {
  const uint32_t k = (i + frame) % kPx;
  return Color(static_cast<uint8_t>(k * 13), static_cast<uint8_t>(250 - k * 11),
               static_cast<uint8_t>((k * 37) & 0xFF), static_cast<uint8_t>(k * 5));
}

// The colour shifty blends toward `to` from `from` for a pixel `elapsed` into
// a fade of `dur`, the same ramp shifty's linear easing runs.
Color ramp(Color from, Color to, uint32_t elapsed, uint32_t dur) {
  if (elapsed >= dur) return to;
  return mixColorLinear(from, to, computeLinearFactor(easeStep(elapsed, dur, Easing::Linear), dur));
}

struct Rig {
  FrameBuffer fb;
  FrameBuffer other;
  BehaviorContext ctx;
  ShiftyExpression shifty{&fb, 100};
  uint32_t frame = 0;
  uint32_t now = 10000;

  explicit Rig(uint32_t fillMode) {
    fb.pixelCount = kPx;
    fb.buffer.assign(kPx, Color());
    other.pixelCount = kPx;
    other.buffer.assign(kPx, Color());
    ctx.expressionFrameBuffers = {&fb, &other};
    shifty.setBehaviorContext(&ctx);
    set_mock_millis(now);
    shifty.configure({kShifted}, 100000, 100000, TARGET_SHADE);
    shifty.configureFromParameters({{"fillMode", fillMode},
                                    {"fadeDuration", kFadeMs / 1000},
                                    {"shiftDurationMin", kHoldMs / 1000},
                                    {"shiftDurationMax", kHoldMs / 1000},
                                    {"easing", 0},
                                    {"opacity", 100}});
  }

  // One compositor frame: the base moves, then shifty runs over it.
  std::vector<Color> step() {
    now += kStepMs;
    ++frame;
    set_mock_millis(now);
    for (size_t i = 0; i < kPx; ++i) fb.buffer[i] = base(i, frame);
    std::vector<Color> live = fb.buffer;
    shifty.control();
    shifty.draw();
    return live;
  }
};

void assertPixel(Color expected, Color actual, uint32_t frame, size_t i) {
  char msg[48];
  snprintf(msg, sizeof msg, "frame %u pixel %zu", static_cast<unsigned>(frame), i);
  TEST_ASSERT_TRUE_MESSAGE(expected == actual, msg);
}

// Uniform fill: every pixel's fade runs on one clock.
void test_uniform_fill_fades_against_the_live_base() {
  Rig r(0);
  r.step();
  TEST_ASSERT_TRUE(r.shifty.trigger());
  const uint32_t toStart = r.now;
  int midFrames = 0;
  while (r.now + kStepMs - toStart < kFadeMs) {
    const auto live = r.step();
    const uint32_t elapsed = r.now - toStart;
    if (elapsed > 0 && elapsed < kFadeMs) ++midFrames;
    for (size_t i = 0; i < kPx; ++i) {
      assertPixel(ramp(live[i], kShifted, elapsed, kFadeMs), r.fb.buffer[i], r.frame, i);
    }
  }
  TEST_ASSERT_GREATER_THAN(10, midFrames);
  // The hold paints the shifted colour over the live base.
  for (uint32_t t = 0; t < kHoldMs + 2 * kStepMs; t += kStepMs) {
    r.step();
    for (size_t i = 0; i < kPx; ++i) assertPixel(kShifted, r.fb.buffer[i], r.frame, i);
  }
  // Fading back targets the live base each frame.
  const uint32_t backStart = r.now;
  bool sawBase = false;
  for (int k = 0; k < 80; ++k) {
    const auto live = r.step();
    const uint32_t elapsed = r.now - backStart;
    for (size_t i = 0; i < kPx; ++i) {
      const Color expected = elapsed >= kFadeMs ? live[i] : ramp(kShifted, live[i], elapsed, kFadeMs);
      assertPixel(expected, r.fb.buffer[i], r.frame, i);
    }
    if (elapsed >= kFadeMs) sawBase = true;
  }
  TEST_ASSERT_TRUE(sawBase);
}

// Fill Up: each pixel starts its half-length fade at its own stagger, still
// against the live base.
void test_directional_fill_fades_against_the_live_base() {
  Rig r(1);
  r.step();
  TEST_ASSERT_TRUE(r.shifty.trigger());
  const uint32_t perFade = kFadeMs / 2;
  const uint32_t window = kFadeMs + perFade;
  auto offset = [&](size_t i) { return static_cast<uint32_t>(i) * perFade / (kPx - 1); };
  const uint32_t toStart = r.now;
  int midPixels = 0;
  while (r.now + kStepMs - toStart < window) {
    const auto live = r.step();
    const uint32_t elapsed = r.now - toStart;
    for (size_t i = 0; i < kPx; ++i) {
      const Color expected =
          elapsed < offset(i) ? live[i] : ramp(live[i], kShifted, elapsed - offset(i), perFade);
      if (elapsed > offset(i) && elapsed - offset(i) < perFade) ++midPixels;
      assertPixel(expected, r.fb.buffer[i], r.frame, i);
    }
  }
  TEST_ASSERT_GREATER_THAN(100, midPixels);
  for (uint32_t t = 0; t < kHoldMs + 2 * kStepMs; t += kStepMs) r.step();
  const uint32_t backStart = r.now;
  bool settled = false;
  for (int k = 0; k < 80; ++k) {
    const auto live = r.step();
    const uint32_t elapsed = r.now - backStart;
    bool allBase = true;
    for (size_t i = 0; i < kPx; ++i) {
      const Color expected =
          elapsed < offset(i) ? kShifted : ramp(kShifted, live[i], elapsed - offset(i), perFade);
      assertPixel(expected, r.fb.buffer[i], r.frame, i);
      allBase = allBase && r.fb.buffer[i] == live[i];
    }
    if (elapsed < window) settled = settled || allBase;
  }
  TEST_ASSERT_TRUE(settled);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_uniform_fill_fades_against_the_live_base);
  RUN_TEST(test_directional_fill_fades_against_the_live_base);
  return UNITY_END();
}
