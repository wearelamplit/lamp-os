// Drives the real PulseExpression over native frame buffers with a mock clock:
// single-surface output pinned to golden hashes, and the both-surface pulse
// running base then shade as one strip.

#include <unity.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/expressions/expression.cpp"
#include "../../src/expressions/pulse/pulse_expression.cpp"
#include "expressions/surface_plan.hpp"

using namespace lamp;

namespace lamp {
OverrideAggregate overrides;
}  // namespace lamp

namespace {

int g_fired = 0;

constexpr uint8_t kShadePx = 38;
constexpr uint8_t kBasePx = 35;
constexpr uint32_t kFrameMs = 16;
const Color kBackground{0, 0, 0, 0};
const Color kPulse{200, 40, 10, 0};

// The standard lamp: base logical 0 is its top, shade logical 0 its bottom.
// standard_lamp.hpp declares the base strip reversed and the shade not.
struct Rig {
  Adafruit_NeoPixel shadeStrip;
  Adafruit_NeoPixel baseStrip;
  FrameBuffer shade;
  FrameBuffer base;
  BehaviorContext ctx;

  Rig() {
    // reversed flags as StandardLamp declares them in standard_lamp.hpp.
    shade.begin({}, {{&shadeStrip, "Shade", 0, kShadePx, /*reversed=*/false}});
    base.begin({}, {{&baseStrip, "Base", 0, kBasePx, /*reversed=*/true}});
    clear();
    ctx.expressionFrameBuffers = {&shade, &base};
    ctx.expressionManager = reinterpret_cast<ExpressionManager*>(this);
    overrides.base.setOperatorEditing(false);
    overrides.shade.setOperatorEditing(false);
    g_fired = 0;
  }

  void clear() {
    shade.buffer.assign(kShadePx, kBackground);
    base.buffer.assign(kBasePx, kBackground);
  }
};

uint32_t fnv(uint32_t h, const std::vector<Color>& buf) {
  for (const Color& c : buf) {
    for (uint8_t b : {c.r, c.g, c.b, c.w}) {
      h ^= b;
      h *= 16777619u;
    }
  }
  return h;
}

struct RunResult {
  uint32_t hash = 2166136261u;
  uint32_t frames = 0;
};

// One compositor tick per frame: background, control(), draw() while playing.
// `onFrame` sees each drawn frame with its clock.
RunResult run(Rig& rig, PulseExpression& p, uint32_t maxFrames,
              const std::function<void(uint32_t)>& onFrame = nullptr) {
  RunResult r;
  uint32_t now = 1000;
  set_mock_millis(now);
  TEST_ASSERT_TRUE(p.trigger());
  for (uint32_t f = 0; f < maxFrames; ++f) {
    now += kFrameMs;
    set_mock_millis(now);
    rig.clear();
    p.control();
    if (p.animationState == STOPPED) break;
    p.draw();
    r.hash = fnv(fnv(r.hash, rig.shade.buffer), rig.base.buffer);
    ++r.frames;
    if (onFrame) onFrame(now);
  }
  return r;
}

void build(Rig& rig, PulseExpression& p, ExpressionTarget target,
           const ExpressionParams& params) {
  p.setBehaviorContext(&rig.ctx);
  p.configure({kPulse}, 300, 900, target);
  p.autoTriggerEnabled = false;
  p.configureFromParameters(params);
}

// Built the way ExpressionManager::addExpression builds a TARGET_BOTH pulse.
std::unique_ptr<PulseExpression> buildBoth(Rig& rig, const ExpressionParams& params) {
  const SurfacePlan plan =
      surfacePlan(&PulseExpression::classDescriptor(), TARGET_BOTH, &rig.shade, &rig.base);
  auto p = std::make_unique<PulseExpression>(plan.buffers[0]);
  if (plan.above) p->joinSurfaceAbove(plan.above);
  build(rig, *p, TARGET_BOTH, params);
  return p;
}

// Height of the brightest physical pixel, read off the strips after a flush:
// base pixel p is height p, shade pixel p is counted down from the lamp top.
// -1 when nothing is lit.
int peakPosition(Rig& rig) {
  int best = -1;
  uint32_t bestR = 0;
  auto scan = [&](FrameBuffer& fb, Adafruit_NeoPixel& strip, auto height) {
    strip.reset();
    fb.previousBuffer.clear();
    fb.flush();
    for (const auto& call : strip.pixelCalls) {
      const uint32_t r = (call.c >> 16) & 0xFF;
      if (r > bestR) { bestR = r; best = height(call.n); }
    }
  };
  scan(rig.base, rig.baseStrip, [](int p) { return p; });
  scan(rig.shade, rig.shadeStrip, [](int p) { return kBasePx + p; });
  return best;
}

// Time for the peak to travel from the pixel after height `from` to height
// `to`. The first pixel peaks while the wave is still entering off the strip.
uint32_t crossingMs(Rig& rig, PulseExpression& p, int from, int to) {
  const int second = from + (to > from ? 1 : -1);
  uint32_t startMs = 0;
  uint32_t topMs = 0;
  run(rig, p, 20000, [&](uint32_t now) {
    const int peak = peakPosition(rig);
    if (peak == second && startMs == 0) startMs = now;
    if (peak == to && topMs == 0) topMs = now;
  });
  TEST_ASSERT_TRUE(startMs > 0 && topMs > startMs);
  return topMs - startMs;
}

// Distinct peak heights in the order the wave visits them.
std::vector<int> peakTrail(Rig& rig, PulseExpression& p) {
  std::vector<int> trail;
  run(rig, p, 20000, [&](uint32_t) {
    const int peak = peakPosition(rig);
    if (peak >= 0 && (trail.empty() || trail.back() != peak)) trail.push_back(peak);
  });
  return trail;
}

void assertSweep(const std::vector<int>& trail, int from, int to) {
  TEST_ASSERT_FALSE(trail.empty());
  TEST_ASSERT_EQUAL_INT(from, trail.front());
  TEST_ASSERT_EQUAL_INT(to, trail.back());
  const int step = to > from ? 1 : -1;
  for (size_t i = 1; i < trail.size(); ++i) TEST_ASSERT_EQUAL_INT(step, trail[i] - trail[i - 1]);
}

bool anyLit(const std::vector<Color>& buf, int from, int to) {
  for (int i = from; i <= to; ++i) {
    if (!(buf[i] == kBackground)) return true;
  }
  return false;
}

}  // namespace

namespace lamp {
void ExpressionManager::onExpressionFired(Expression*) { ++g_fired; }
}  // namespace lamp

void setUp() {}
void tearDown() {}

// Golden hashes pin single-surface output byte for byte. The base pin is the
// rising walk.
void test_shade_only_unchanged() {
  Rig rig;
  PulseExpression p(&rig.shade);
  build(rig, p, TARGET_SHADE, {{"pulseSpeed", 6}, {"size", 4}});
  const RunResult r = run(rig, p, 5000);
  TEST_ASSERT_EQUAL_UINT32(659u, r.frames);
  TEST_ASSERT_EQUAL_HEX32(0xAC8B11CAu, r.hash);
}

void test_base_only_pinned() {
  Rig rig;
  PulseExpression p(&rig.base);
  build(rig, p, TARGET_BASE, {{"pulseSpeed", 2}, {"size", 9}, {"easing", 1}});
  const RunResult r = run(rig, p, 5000);
  TEST_ASSERT_EQUAL_UINT32(514u, r.frames);
  TEST_ASSERT_EQUAL_HEX32(0x7A02113Eu, r.hash);
}

void test_shade_region_continuous_unchanged() {
  Rig rig;
  PulseExpression p(&rig.shade);
  build(rig, p, TARGET_SHADE,
        {{"pulseSpeed", 4}, {"fullStrip", 0}, {"posMin", 5}, {"posMax", 30},
         {"loop", 1}, {"easing", 2}, {"opacity", 60}});
  const RunResult r = run(rig, p, 5000);
  TEST_ASSERT_EQUAL_UINT32(1027u, r.frames);
  TEST_ASSERT_EQUAL_HEX32(0x164D9945u, r.hash);
}

void test_plan_both_spanning_type_builds_one_base_instance() {
  Rig rig;
  const SurfacePlan plan =
      surfacePlan(&PulseExpression::classDescriptor(), TARGET_BOTH, &rig.shade, &rig.base);
  TEST_ASSERT_EQUAL_UINT8(1, plan.count);
  TEST_ASSERT_EQUAL_PTR(&rig.base, plan.buffers[0]);
  TEST_ASSERT_EQUAL_PTR(&rig.shade, plan.above);
}

void test_plan_both_other_type_builds_one_per_surface() {
  Rig rig;
  const ExpressionDescriptor perSurface{};
  for (const ExpressionDescriptor* d : {&perSurface, (const ExpressionDescriptor*)nullptr}) {
    const SurfacePlan plan = surfacePlan(d, TARGET_BOTH, &rig.shade, &rig.base);
    TEST_ASSERT_EQUAL_UINT8(2, plan.count);
    TEST_ASSERT_EQUAL_PTR(&rig.shade, plan.buffers[0]);
    TEST_ASSERT_EQUAL_PTR(&rig.base, plan.buffers[1]);
    TEST_ASSERT_NULL(plan.above);
  }
}

void test_plan_single_surface_targets_unjoined() {
  Rig rig;
  const ExpressionDescriptor* d = &PulseExpression::classDescriptor();
  SurfacePlan plan = surfacePlan(d, TARGET_SHADE, &rig.shade, &rig.base);
  TEST_ASSERT_EQUAL_UINT8(1, plan.count);
  TEST_ASSERT_EQUAL_PTR(&rig.shade, plan.buffers[0]);
  TEST_ASSERT_NULL(plan.above);
  plan = surfacePlan(d, TARGET_BASE, &rig.shade, &rig.base);
  TEST_ASSERT_EQUAL_UINT8(1, plan.count);
  TEST_ASSERT_EQUAL_PTR(&rig.base, plan.buffers[0]);
  TEST_ASSERT_NULL(plan.above);
  plan = surfacePlan(d, static_cast<ExpressionTarget>(0), &rig.shade, &rig.base);
  TEST_ASSERT_EQUAL_UINT8(0, plan.count);
}

// Every instance the plan builds auto-fires in the same tick, and each fire
// reaches ExpressionManager::onExpressionFired, which runs the cascade and the
// event. A both-surface pulse fires once; a per-surface type fires twice and
// leans on the manager's dedup ring.
int firesForPlan(const ExpressionDescriptor* d) {
  Rig rig;
  set_mock_millis(0);
  const SurfacePlan plan = surfacePlan(d, TARGET_BOTH, &rig.shade, &rig.base);
  std::vector<std::unique_ptr<PulseExpression>> built;
  for (uint8_t b = 0; b < plan.count; ++b) {
    built.push_back(std::make_unique<PulseExpression>(plan.buffers[b]));
    if (plan.above) built.back()->joinSurfaceAbove(plan.above);
    build(rig, *built.back(), TARGET_BOTH, {});
    built.back()->autoTriggerEnabled = true;
  }
  set_mock_millis(2000000);
  for (auto& e : built) e->control();
  return g_fired;
}

// An auto-trigger pulse whose control() is skipped for over 24.8 days, as
// home mode skips a disabled type, fires on its first control() after.
void test_auto_trigger_fires_after_a_long_skip() {
  Rig rig;
  g_fired = 0;
  set_mock_millis(0);
  PulseExpression p(&rig.base);
  build(rig, p, TARGET_BASE, {});
  p.autoTriggerEnabled = true;
  set_mock_millis((1u << 31) + 2000000);
  p.control();
  TEST_ASSERT_EQUAL_INT(1, g_fired);
}

void test_both_fires_once_per_logical_trigger() {
  TEST_ASSERT_EQUAL_INT(1, firesForPlan(&PulseExpression::classDescriptor()));
  const ExpressionDescriptor perSurface{};
  TEST_ASSERT_EQUAL_INT(2, firesForPlan(&perSurface));
}

// The wave climbs the base bottom to top and carries straight on into the
// shade: the peak starts on the base's bottom physical pixel, never moves
// backward or skips a pixel, crosses from the base top to the shade bottom, and
// ends on the shade top.
void test_both_wave_crosses_join_continuously() {
  Rig rig;
  auto p = buildBoth(rig, {{"pulseSpeed", 10}, {"size", 1}});
  int last = -1;
  bool sawBaseTop = false;
  bool sawShadeBottom = false;
  const RunResult r = run(rig, *p, 20000, [&](uint32_t) {
    const int peak = peakPosition(rig);
    if (peak < 0) return;
    if (last < 0) TEST_ASSERT_EQUAL_INT(0, peak);
    if (last >= 0) {
      TEST_ASSERT_TRUE(peak >= last);
      TEST_ASSERT_TRUE(peak - last <= 1);
    }
    if (peak == kBasePx - 1) sawBaseTop = true;
    if (peak == kBasePx) {
      TEST_ASSERT_TRUE(sawBaseTop);
      sawShadeBottom = true;
    }
    last = peak;
  });
  TEST_ASSERT_TRUE(sawShadeBottom);
  TEST_ASSERT_EQUAL_INT(kBasePx + kShadePx - 1, last);
  TEST_ASSERT_TRUE(p->isAnimationComplete());
  TEST_ASSERT_TRUE(r.frames > 0);
}

// Base pixels light first, the shade stays dark until the wave nears the
// join, and a Region zone holds on each surface: base 5..20 runs into shade
// 5..20 with nothing outside either touched.
void test_both_maps_each_surface_zone() {
  Rig rig;
  auto p = buildBoth(rig, {{"pulseSpeed", 8}, {"size", 1}, {"fullStrip", 0},
                           {"posMin", 5}, {"posMax", 20}});
  bool baseLitBeforeShade = false;
  bool shadeLit = false;
  int crossings = 0;
  bool lastWasBase = true;
  run(rig, *p, 20000, [&](uint32_t) {
    TEST_ASSERT_FALSE(anyLit(rig.base.buffer, 0, 4));
    TEST_ASSERT_FALSE(anyLit(rig.base.buffer, 21, kBasePx - 1));
    TEST_ASSERT_FALSE(anyLit(rig.shade.buffer, 0, 4));
    TEST_ASSERT_FALSE(anyLit(rig.shade.buffer, 21, kShadePx - 1));
    const bool baseLit = anyLit(rig.base.buffer, 5, 20);
    const bool shadeNow = anyLit(rig.shade.buffer, 5, 20);
    if (baseLit && !shadeLit && !shadeNow) baseLitBeforeShade = true;
    if (shadeNow) shadeLit = true;
    // The base zone tops out at logical 5; the shade zone starts at logical 5.
    uint8_t baseTop = rig.base.buffer[5].r;
    uint8_t shadeBottom = rig.shade.buffer[5].r;
    const bool peakInBase = baseTop >= shadeBottom;
    if (baseTop > 0 || shadeBottom > 0) {
      if (lastWasBase && !peakInBase) ++crossings;
      lastWasBase = peakInBase;
    }
  });
  TEST_ASSERT_TRUE(baseLitBeforeShade);
  TEST_ASSERT_TRUE(shadeLit);
  TEST_ASSERT_EQUAL_INT(1, crossings);
}

// pulseSpeed is the time to cross the whole strip: base and shade together
// take as long as one surface alone at the same setting. Timed from the peak
// reaching the second pixel, since the first pixel peaks while the wave is
// still entering off the strip.
void test_both_speed_spans_whole_strip() {
  const ExpressionParams params{{"pulseSpeed", 10}, {"size", 1}};
  // Per-pixel step is pulseSpeed over the strip's pixel count, in whole ms.
  auto expectMs = [](uint32_t pixels) { return (10000u / pixels) * (pixels - 2); };

  Rig both;
  auto joined = buildBoth(both, params);
  const uint32_t bothMs = crossingMs(both, *joined, 0, kBasePx + kShadePx - 1);

  Rig single;
  PulseExpression shade(&single.shade);
  build(single, shade, TARGET_SHADE, params);
  const uint32_t shadeMs = crossingMs(single, shade, kBasePx, kBasePx + kShadePx - 1);

  TEST_ASSERT_UINT32_WITHIN(2 * kFrameMs, expectMs(kBasePx + kShadePx), bothMs);
  TEST_ASSERT_UINT32_WITHIN(2 * kFrameMs, expectMs(kShadePx), shadeMs);
  TEST_ASSERT_UINT32_WITHIN(400, shadeMs, bothMs);
}

// An operator editing the shade colour pauses the shade half only.
void test_both_editing_one_surface_pauses_only_that_half() {
  Rig rig;
  auto p = buildBoth(rig, {{"pulseSpeed", 6}});
  overrides.shade.setOperatorEditing(true);
  bool baseLit = false;
  run(rig, *p, 20000, [&](uint32_t) {
    TEST_ASSERT_FALSE(anyLit(rig.shade.buffer, 0, kShadePx - 1));
    if (anyLit(rig.base.buffer, 0, kBasePx - 1)) baseLit = true;
  });
  overrides.shade.setOperatorEditing(false);
  TEST_ASSERT_TRUE(baseLit);
}

// At the default setting the floor sets the pace: the joined pulse climbs the
// whole lamp in about the time the longer surface took alone.
void test_both_default_speed_matches_one_surface() {
  const ExpressionParams params{{"pulseSpeed", 3}, {"size", 1}};
  Rig both;
  auto joined = buildBoth(both, params);
  const uint32_t bothMs = crossingMs(both, *joined, 0, kBasePx + kShadePx - 1);
  Rig single;
  PulseExpression shade(&single.shade);
  build(single, shade, TARGET_SHADE, params);
  const uint32_t shadeMs = crossingMs(single, shade, kBasePx, kBasePx + kShadePx - 1);
  // The floor's per-pixel step scales the longer surface's floor to the strip.
  const uint32_t strip = kBasePx + kShadePx;
  const uint32_t expectBoth = (kPulseFloorMsPerPx * kShadePx / strip) * (strip - 2);
  TEST_ASSERT_UINT32_WITHIN(2 * kFrameMs, expectBoth, bothMs);
  TEST_ASSERT_UINT32_WITHIN(250, shadeMs, bothMs);
}

// With the base editor open a joined pulse still fires and plays on the shade.
void test_both_fires_with_base_editor_open() {
  Rig rig;
  auto p = buildBoth(rig, {{"pulseSpeed", 6}});
  overrides.base.setOperatorEditing(true);
  bool shadeLit = false;
  run(rig, *p, 20000, [&](uint32_t) {
    TEST_ASSERT_FALSE(anyLit(rig.base.buffer, 0, kBasePx - 1));
    if (anyLit(rig.shade.buffer, 0, kShadePx - 1)) shadeLit = true;
  });
  overrides.base.setOperatorEditing(false);
  TEST_ASSERT_TRUE(shadeLit);
}

void test_both_editors_open_refuses_trigger() {
  Rig rig;
  auto p = buildBoth(rig, {});
  overrides.base.setOperatorEditing(true);
  overrides.shade.setOperatorEditing(true);
  const bool fired = p->trigger();
  overrides.base.setOperatorEditing(false);
  overrides.shade.setOperatorEditing(false);
  TEST_ASSERT_FALSE(fired);
}

// Direction Down drips from the shade top through to the base bottom.
void test_both_down_drips_shade_top_to_base_bottom() {
  Rig rig;
  auto p = buildBoth(rig, {{"pulseSpeed", 10}, {"size", 1}, {"travel", 1}});
  assertSweep(peakTrail(rig, *p), kBasePx + kShadePx - 1, 0);
}

// A shade-only pulse rises Up and falls Down.
void test_shade_only_up_rises_down_falls() {
  for (uint32_t travel : {0u, 1u}) {
    Rig rig;
    PulseExpression shade(&rig.shade);
    build(rig, shade, TARGET_SHADE, {{"pulseSpeed", 6}, {"size", 1}, {"travel", travel}});
    const std::vector<int> trail = peakTrail(rig, shade);
    if (travel == 0) assertSweep(trail, kBasePx, kBasePx + kShadePx - 1);
    else assertSweep(trail, kBasePx + kShadePx - 1, kBasePx);
  }
}

// A base-only pulse rises Up and falls Down.
void test_base_only_up_rises_down_falls() {
  for (uint32_t travel : {0u, 1u}) {
    Rig rig;
    PulseExpression base(&rig.base);
    build(rig, base, TARGET_BASE, {{"pulseSpeed", 6}, {"size", 1}, {"travel", travel}});
    const std::vector<int> trail = peakTrail(rig, base);
    if (travel == 0) assertSweep(trail, 0, kBasePx - 1);
    else assertSweep(trail, kBasePx - 1, 0);
  }
}

void test_plan_base_without_pixels_plans_per_surface() {
  Rig rig;
  rig.base.pixelCount = 0;
  const SurfacePlan plan =
      surfacePlan(&PulseExpression::classDescriptor(), TARGET_BOTH, &rig.shade, &rig.base);
  TEST_ASSERT_EQUAL_UINT8(2, plan.count);
  TEST_ASSERT_NULL(plan.above);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_shade_only_unchanged);
  RUN_TEST(test_base_only_pinned);
  RUN_TEST(test_shade_region_continuous_unchanged);
  RUN_TEST(test_plan_both_spanning_type_builds_one_base_instance);
  RUN_TEST(test_plan_both_other_type_builds_one_per_surface);
  RUN_TEST(test_plan_single_surface_targets_unjoined);
  RUN_TEST(test_both_fires_once_per_logical_trigger);
  RUN_TEST(test_auto_trigger_fires_after_a_long_skip);
  RUN_TEST(test_both_wave_crosses_join_continuously);
  RUN_TEST(test_both_maps_each_surface_zone);
  RUN_TEST(test_both_speed_spans_whole_strip);
  RUN_TEST(test_both_editing_one_surface_pauses_only_that_half);
  RUN_TEST(test_both_default_speed_matches_one_surface);
  RUN_TEST(test_both_fires_with_base_editor_open);
  RUN_TEST(test_both_editors_open_refuses_trigger);
  RUN_TEST(test_both_down_drips_shade_top_to_base_bottom);
  RUN_TEST(test_shade_only_up_rises_down_falls);
  RUN_TEST(test_base_only_up_rises_down_falls);
  RUN_TEST(test_plan_base_without_pixels_plans_per_surface);
  return UNITY_END();
}
