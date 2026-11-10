// The first frame an expression draws after weeks without one steps like a
// normal frame. Two twins run the same frames, then one waits the longest
// normal frame (lampos::kMaxFrameDeltaMs) and the other three weeks, as home
// mode can hold an expression; both must paint the same.

#include <unity.h>

#include <cstdint>
#include <vector>

#include <lampos/clock.hpp>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/expressions/breathing/breathing_expression.cpp"
#include "../../src/expressions/expression.cpp"
#include "../../src/expressions/pulse/pulse_expression.cpp"
#include "../../src/expressions/shimmer/shimmer_expression.cpp"
#include "../../src/expressions/spotty/spotty_expression.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"

using namespace lamp;

namespace lamp {
OverrideAggregate overrides;
void ExpressionManager::onExpressionFired(Expression*) {}
}  // namespace lamp

namespace {

constexpr uint8_t kPixels = 24;
constexpr uint32_t kFrameMs = 16;
constexpr uint32_t kStartMs = 1000;
constexpr uint32_t kThreeWeeksMs = 21u * 24 * 60 * 60 * 1000;
const Color kAnchor(120, 60, 0, 0);
const std::vector<Color> kColors = {Color(0, 0, 255, 0), Color(255, 0, 0, 0)};

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

// An expression on its own surfaces with a fixed random stream, so two of
// them run frame for frame alike.
template <typename E>
struct Twin {
  struct Seeded : E {
    explicit Seeded(FrameBuffer* fb) : E(fb) { this->rng = FastRng(0x5EEDu); }
  };
  Surfaces s;
  Seeded e{&s.shade};

  Twin(const ExpressionParams& params, bool transient) {
    e.configure(kColors, 60, 900, TARGET_SHADE);
    e.configureFromParameters(params);
    e.isTransient = transient;
    e.autoTriggerEnabled = !transient;
    e.setBehaviorContext(&s.ctx);
    set_mock_millis(kStartMs);
    e.trigger();
  }

  const std::vector<Color>& frame(uint32_t nowMs) {
    set_mock_millis(nowMs);
    s.shade.buffer.assign(kPixels, kAnchor);
    e.control();
    e.draw();
    return s.shade.buffer;
  }
};

void assertSameFrame(const std::vector<Color>& want, const std::vector<Color>& got) {
  TEST_ASSERT_EQUAL_size_t(want.size(), got.size());
  for (size_t i = 0; i < want.size(); ++i) {
    TEST_ASSERT_EQUAL_HEX8(want[i].r, got[i].r);
    TEST_ASSERT_EQUAL_HEX8(want[i].g, got[i].g);
    TEST_ASSERT_EQUAL_HEX8(want[i].b, got[i].b);
    TEST_ASSERT_EQUAL_HEX8(want[i].w, got[i].w);
  }
}

bool anyPainted(const std::vector<Color>& frame) {
  for (const Color& c : frame) {
    if (!(c == kAnchor)) return true;
  }
  return false;
}

// Runs both twins to `pausedMs`, then draws `after` frames following the
// pause, each twin's on its own clock, and checks they match frame by frame.
template <typename E>
void checkResumesLikeANormalFrame(const ExpressionParams& params, bool transient,
                                  uint32_t pausedMs, int after) {
  Twin<E> normal(params, transient);
  Twin<E> frozen(params, transient);
  for (uint32_t now = kStartMs; now <= pausedMs; now += kFrameMs) {
    assertSameFrame(normal.frame(now), frozen.frame(now));
  }
  const uint32_t lastMs = pausedMs - (pausedMs - kStartMs) % kFrameMs;
  bool painted = false;
  for (int i = 0; i < after; ++i) {
    const uint32_t step = lampos::kMaxFrameDeltaMs + i * kFrameMs;
    const std::vector<Color> want = normal.frame(lastMs + step);
    painted = painted || anyPainted(want);
    assertSameFrame(want, frozen.frame(lastMs + kThreeWeeksMs + step));
  }
  TEST_ASSERT_TRUE(painted);
}

}  // namespace

void setUp() { set_mock_millis(kStartMs); }
void tearDown() {}

void test_breathing_resumes_mid_breath() {
  checkResumesLikeANormalFrame<BreathingExpression>(
      {{"breathSpeed", 10}, {"opacity", 100}}, false, kStartMs + 2500, 4);
}

void test_spotty_resumes_without_respawning_its_spots() {
  checkResumesLikeANormalFrame<SpottyExpression>(
      {{"count", 3}, {"size", 3}, {"spotSpeed", 10}, {"opacity", 100}}, false,
      kStartMs + 1500, 4);
}

void test_shimmer_resumes_mid_flicker() {
  checkResumesLikeANormalFrame<ShimmerExpression>({{"opacity", 100}}, false,
                                                  kStartMs + 1500, 4);
}

void test_pulse_resumes_mid_travel() {
  checkResumesLikeANormalFrame<PulseExpression>(
      {{"pulseSpeed", 20}, {"size", 4}, {"loop", 1}, {"opacity", 100}}, false,
      kStartMs + 600, 4);
}

// A 2 s burst paused just after it starts to fall comes back with a normal
// step of the fall. The burst's own length runs on uptime, so only that first
// frame compares.
void test_shimmer_burst_resumes_mid_fall() {
  checkResumesLikeANormalFrame<ShimmerExpression>(
      {{"fire", 0}, {"count", 5}, {"durationMax", 2000}, {"opacity", 100}}, true,
      kStartMs + 1100, 1);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_breathing_resumes_mid_breath);
  RUN_TEST(test_spotty_resumes_without_respawning_its_spots);
  RUN_TEST(test_shimmer_resumes_mid_flicker);
  RUN_TEST(test_shimmer_burst_resumes_mid_fall);
  RUN_TEST(test_pulse_resumes_mid_travel);
  return UNITY_END();
}
