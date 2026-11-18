// A transient shifty is a one-shot tint: it fades back over `durationMax`
// seconds and ends without chaining glitchy. A configured shifty with the
// same keys keeps its fadeDuration fade back and its glitchy chain.

#include <unity.h>

#include <string>
#include <vector>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/expressions/expression.cpp"
#include "../../src/expressions/shifty/shifty_expression.cpp"

namespace lamp {
OverrideAggregate overrides;
std::vector<std::string> s_chained;
void ExpressionManager::onExpressionFired(Expression*) {}
bool ExpressionManager::triggerExpression(const std::string& type) {
  s_chained.push_back(type);
  return true;
}
}  // namespace lamp

using namespace lamp;

namespace {

constexpr uint8_t kPx = 12;
constexpr uint32_t kFadeS = 4;
constexpr uint32_t kHoldS = 20;
constexpr uint32_t kBackS = 120;
constexpr uint32_t kStepMs = 100;
const Color kBase(0, 0, 0, 120);
const Color kTint(0, 255, 128, 0);

struct Rig {
  FrameBuffer fb;
  FrameBuffer other;
  BehaviorContext ctx;
  ExpressionManager manager;
  ShiftyExpression shifty{&fb, 100};
  uint32_t now = 10000;

  Rig(bool transient, uint32_t fillMode) {
    s_chained.clear();
    for (FrameBuffer* b : {&fb, &other}) {
      b->pixelCount = kPx;
      b->buffer.assign(kPx, kBase);
    }
    ctx.expressionFrameBuffers = {&fb, &other};
    ctx.expressionManager = &manager;
    shifty.setBehaviorContext(&ctx);
    set_mock_millis(now);
    shifty.configure({kTint}, 60, 900, TARGET_SHADE);
    shifty.configureFromParameters({{"fillMode", fillMode},
                                    {"fadeDuration", kFadeS},
                                    {"shiftDurationMin", kHoldS},
                                    {"shiftDurationMax", kHoldS},
                                    {"durationMax", kBackS},
                                    {"opacity", 100}});
    shifty.isTransient = transient;
    shifty.autoTriggerEnabled = !transient;
  }

  void step() {
    now += kStepMs;
    set_mock_millis(now);
    fb.buffer.assign(kPx, kBase);
    shifty.control();
    shifty.draw();
  }

  // Runs from trigger until the cycle stops; returns its length, and the
  // length of the stretch the first pixel spent between tint and base after
  // the hold.
  uint32_t runCycle(uint32_t& fadeBackMs) {
    TEST_ASSERT_TRUE(shifty.trigger());
    const uint32_t start = now;
    uint32_t lastTinted = 0;
    uint32_t lastBetween = 0;
    while (shifty.animationState != STOPPED) {
      step();
      const Color p = fb.buffer[0];
      if (p == kTint) lastTinted = now;
      else if (!(p == kBase)) lastBetween = now;
      TEST_ASSERT_TRUE_MESSAGE(now - start < 400000, "shifty never stopped");
    }
    fadeBackMs = lastBetween > lastTinted ? lastBetween - lastTinted : 0;
    return now - start;
  }
};

uint32_t s(uint32_t seconds) { return seconds * 1000; }

}  // namespace

void setUp() {}
void tearDown() {}

void test_a_transient_shifty_fades_back_over_duration_max() {
  Rig r(/*transient=*/true, /*fillMode=*/0);
  uint32_t fadeBackMs = 0;
  const uint32_t cycle = r.runCycle(fadeBackMs);
  const uint32_t expected = s(kFadeS + kHoldS + kBackS);
  TEST_ASSERT_UINT32_WITHIN(3 * kStepMs, expected, cycle);
  TEST_ASSERT_UINT32_WITHIN(3 * kStepMs, s(kBackS), fadeBackMs);
}

void test_a_transient_shifty_chains_no_glitchy() {
  Rig r(/*transient=*/true, /*fillMode=*/0);
  uint32_t fadeBackMs = 0;
  r.runCycle(fadeBackMs);
  for (int i = 0; i < 10; ++i) r.step();
  TEST_ASSERT_EQUAL_UINT32(0, s_chained.size());
}

// The same keys on a configured shifty: the fade back stays fadeDuration and
// the cycle still chains glitchy.
void test_a_configured_shifty_ignores_duration_max_and_chains_glitchy() {
  Rig r(/*transient=*/false, /*fillMode=*/0);
  uint32_t fadeBackMs = 0;
  const uint32_t cycle = r.runCycle(fadeBackMs);
  TEST_ASSERT_UINT32_WITHIN(3 * kStepMs, s(kFadeS + kHoldS + kFadeS), cycle);
  r.step();
  TEST_ASSERT_EQUAL_UINT32(1, s_chained.size());
  TEST_ASSERT_EQUAL_STRING("glitchy", s_chained[0].c_str());
}

// A directional transient staggers its fade back over the longer length too:
// half of it per pixel, each pixel starting up to another half later.
void test_a_transient_bloom_staggers_over_duration_max() {
  Rig r(/*transient=*/true, /*fillMode=*/3);
  uint32_t fadeBackMs = 0;
  const uint32_t cycle = r.runCycle(fadeBackMs);
  TEST_ASSERT_TRUE(cycle > s(kFadeS + kHoldS + kBackS) + s(kBackS) / 3);
  TEST_ASSERT_TRUE(cycle <= s(kFadeS) * 3 / 2 + s(kHoldS) + s(kBackS) * 3 / 2 + 3 * kStepMs);
  TEST_ASSERT_UINT32_WITHIN(3 * kStepMs, s(kBackS) / 2, fadeBackMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_transient_shifty_fades_back_over_duration_max);
  RUN_TEST(test_a_transient_shifty_chains_no_glitchy);
  RUN_TEST(test_a_configured_shifty_ignores_duration_max_and_chains_glitchy);
  RUN_TEST(test_a_transient_bloom_staggers_over_duration_max);
  return UNITY_END();
}
