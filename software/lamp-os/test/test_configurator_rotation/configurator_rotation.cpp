// The real ConfiguratorBehavior drawing a rotating role scene: the shift,
// direction per surface, transitions, raw content, the saved-scene fallback,
// knockout, the quiet-mode reset and home mode. A tick runs control() and then
// draw() unless STOPPED, as the compositor does. The expected frames come from
// a second RotationClock fed the same calls, the unit test_color_rotation pins.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "../../src/behaviors/configurator.cpp"
#include "../../src/behaviors/knockout.cpp"
#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/color_rotation.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"
#include "../../src/util/levels.cpp"
#include "core/home_mode_gate.hpp"

static size_t g_allocs = 0;

void* operator new(size_t n) {
  ++g_allocs;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

using namespace lamp;

void setUp() {}
void tearDown() {}

static constexpr uint8_t kPx = 38;

static int g_line = 0;

static void assertColor(const Color& expected, const Color& actual, size_t i) {
  char msg[40];
  std::snprintf(msg, sizeof msg, "line %d pixel %zu", g_line, i);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(expected.r, actual.r, msg);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(expected.g, actual.g, msg);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(expected.b, actual.b, msg);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(expected.w, actual.w, msg);
}

#define assertFrame(e, a) (g_line = __LINE__, assertFrameAt(e, a))

static void assertFrameAt(const std::vector<Color>& expected, const std::vector<Color>& actual) {
  TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) assertColor(expected[i], actual[i], i);
}

static std::vector<Color> stopsA() {
  return {{0xFF, 0x00, 0x10, 0x80}, {0x00, 0xFF, 0x90, 0x05}, {0x7A, 0x13, 0xFF, 0xC4},
          {0x21, 0xE8, 0x02, 0x3B}};
}
static std::vector<Color> stopsB() {
  return {{0x10, 0xC0, 0xFF, 0x00}, {0xF0, 0x20, 0x00, 0xFF}, {0x55, 0x99, 0x33, 0x11}};
}
static std::vector<Color> rgb() { return {{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}}; }

static RoleScene rotatingScene(const std::vector<Color>& stops, uint8_t periodS,
                               RotationDir dir) {
  return buildRoleScene(kPx, stops, ColorRotation{true, periodS, dir});
}

static std::vector<Color> shifted(const std::vector<Color>& loop, uint32_t phase) {
  const uint32_t offset = loopOffset(phase, loop.size());
  std::vector<Color> out(loop.size());
  for (size_t i = 0; i < loop.size(); ++i) out[i] = sampleLoop(loop, offset, i);
  return out;
}

static std::vector<Color> faded(const std::vector<Color>& from, const std::vector<Color>& to,
                                uint32_t elapsed) {
  std::vector<Color> out(from.size());
  for (size_t i = 0; i < from.size(); ++i) out[i] = fade(from[i], to[i], kDefaultFadeMs, elapsed);
  return out;
}

static bool towardZeroFor(Surface s, RotationDir d) {
  return (s == Surface::Shade && d == RotationDir::Down) ||
         (s == Surface::Base && d == RotationDir::Up);
}

struct Rig {
  FrameBuffer fb;
  ConfiguratorBehavior cfg;
  Surface surface;
  uint32_t now = 10000;
  // The expected clock: advanced on every rotating draw, as the configurator's is.
  RotationClock mirror;

  explicit Rig(Surface s) : cfg(&fb, 120, s), surface(s) {
    fb.pixelCount = kPx;
    fb.buffer.assign(kPx, Color(1, 2, 3, 4));
    fb.defaultColors = buildGradientWithStops(kPx, stopsB());
  }

  void tick(uint32_t ms) {
    now = ms;
    set_mock_millis(ms);
    cfg.control();
    if (cfg.animationState != STOPPED) cfg.draw();
  }
  void step(uint32_t dt = 16) { tick(now + dt); }

  // The frame a rotating draw of `scene` at `ms` shows once no fade runs.
  std::vector<Color> expect(const RoleScene& scene, uint32_t ms) {
    mirror.advance(ms, scene.rotation.periodS, towardZeroFor(surface, scene.rotation.dir));
    return shifted(scene.colors, mirror.phase());
  }

  // Steps `frames` rotating draws of `scene`, keeping the mirror in step.
  void rotate(const RoleScene& scene, int frames) {
    for (int k = 0; k < frames; ++k) {
      step();
      expect(scene, now);
    }
  }

  void write(const RoleScene& scene, uint32_t fadeMs) {
    set_mock_millis(now);
    cfg.showScene(scene, fadeMs);
    cfg.lastWebSocketUpdateTimeMs = now;
  }

  void saveScene(const RoleScene& scene) {
    fb.defaultColors = scene.colors;
    fb.defaultRotation = scene.rotation;
  }
};

// R5, R6: each surface and direction draws the loop shifted by its clock.
void test_frame_is_the_shifted_loop() {
  for (Surface s : {Surface::Shade, Surface::Base}) {
    for (RotationDir d : {RotationDir::Up, RotationDir::Down}) {
      Rig r(s);
      const RoleScene scene = rotatingScene(stopsA(), 5, d);
      r.write(scene, 0);
      TEST_ASSERT_TRUE(r.cfg.rotating());
      for (int k = 0; k < 400; ++k) {
        r.step();
        assertFrame(r.expect(scene, r.now), r.fb.buffer);
      }
    }
  }
}

static int reddest(const std::vector<Color>& frame) {
  int best = 0;
  for (int i = 1; i < static_cast<int>(frame.size()); ++i) {
    if (frame[i].r > frame[best].r) best = i;
  }
  return best;
}

// R6 physically: shade index 0 is its bottom and base index 0 its top, so Up
// carries the red stop to higher shade indices and lower base indices.
void test_up_rises_on_both_surfaces() {
  struct Case {
    Surface s;
    RotationDir d;
    int lo, hi;
  };
  const Case cases[] = {{Surface::Shade, RotationDir::Up, 2, 6},
                        {Surface::Shade, RotationDir::Down, kPx - 6, kPx - 2},
                        {Surface::Base, RotationDir::Up, kPx - 6, kPx - 2},
                        {Surface::Base, RotationDir::Down, 2, 6}};
  for (const Case& c : cases) {
    Rig r(c.s);
    r.write(rotatingScene(rgb(), 5, c.d), 0);
    r.step();
    TEST_ASSERT_TRUE(reddest(r.fb.buffer) <= 1 || reddest(r.fb.buffer) >= kPx - 1);
    for (int k = 0; k < 25; ++k) r.step(16);
    const int at = reddest(r.fb.buffer);
    TEST_ASSERT_TRUE(at >= c.lo && at <= c.hi);
  }
}

// R4: one stop never rotates.
void test_one_stop_scene_is_solid_and_still() {
  Rig r(Surface::Shade);
  const Color c(0x30, 0x60, 0x90, 0xC0);
  r.write(buildRoleScene(kPx, {c}, ColorRotation{true, 5, RotationDir::Up}), 0);
  TEST_ASSERT_FALSE(r.cfg.rotating());
  for (int k = 0; k < 100; ++k) {
    r.step();
    assertFrame(std::vector<Color>(kPx, c), r.fb.buffer);
  }
}

// R10: a change of loop length or direction keeps the frame and the phase.
void test_speed_and_direction_change_keep_the_position() {
  for (bool flip : {false, true}) {
    Rig r(Surface::Shade);
    const RoleScene before = rotatingScene(stopsA(), 40, RotationDir::Up);
    r.write(before, 0);
    for (int k = 0; k < 125; ++k) {
      r.step();
      assertFrame(r.expect(before, r.now), r.fb.buffer);
    }
    const std::vector<Color> held = r.fb.buffer;
    const RoleScene after = flip ? rotatingScene(stopsA(), 40, RotationDir::Down)
                                 : rotatingScene(stopsA(), 5, RotationDir::Up);
    r.write(after, 0);
    r.tick(r.now);
    assertFrame(held, r.fb.buffer);
    r.expect(after, r.now);
    for (int k = 0; k < 200; ++k) {
      r.step();
      assertFrame(r.expect(after, r.now), r.fb.buffer);
    }
  }
}

// R13.
void test_rotating_draw_allocates_nothing() {
  {
    Rig r(Surface::Base);
    r.write(rotatingScene(stopsA(), 5, RotationDir::Up), kDefaultFadeMs);
    g_allocs = 0;
    for (int k = 0; k < 1000; ++k) r.step();
    TEST_ASSERT_TRUE(r.cfg.rotating());
    TEST_ASSERT_EQUAL_size_t(0, g_allocs);
  }
  {
    Rig r(Surface::Shade);
    r.saveScene(rotatingScene(stopsA(), 5, RotationDir::Down));
    r.cfg.colors = r.fb.defaultColors;
    r.step();
    TEST_ASSERT_EQUAL(PLAYING, r.cfg.animationState);
    g_allocs = 0;
    for (int k = 0; k < 1000; ++k) r.step();
    TEST_ASSERT_TRUE(r.cfg.rotating());
    TEST_ASSERT_EQUAL_size_t(0, g_allocs);
  }
}

// R14: every change crossfades over kDefaultFadeMs from the frame on show.
void test_changes_crossfade_from_the_drawn_frame() {
  const RoleScene open = buildRoleScene(kPx, stopsA(), ColorRotation{});
  const RoleScene fast = rotatingScene(stopsA(), 5, RotationDir::Up);
  struct Case {
    RoleScene from, to;
  };
  const Case cases[] = {
      {fast, open},                                            // rotation off
      {fast, rotatingScene(stopsA(), 77, RotationDir::Up)},    // loop length
      {fast, rotatingScene(stopsA(), 5, RotationDir::Down)},   // direction
      {fast, rotatingScene(stopsB(), 5, RotationDir::Up)},     // colours
      {open, fast},                                            // rotation on
  };
  for (const Case& c : cases) {
    Rig r(Surface::Shade);
    r.write(c.from, 0);
    for (int k = 0; k < 90; ++k) {
      r.step();
      if (c.from.rotation.on) r.expect(c.from, r.now);
    }
    const std::vector<Color> held = r.fb.buffer;
    r.write(c.to, kDefaultFadeMs);
    const uint32_t start = r.now;
    r.tick(r.now);
    assertFrame(held, r.fb.buffer);
    if (c.to.rotation.on) r.expect(c.to, r.now);
    while (r.now - start < 160) {
      r.step();
      const std::vector<Color> target = c.to.rotation.on ? r.expect(c.to, r.now) : c.to.colors;
      const uint32_t elapsed = r.now - start;
      assertFrame(elapsed < kDefaultFadeMs ? faded(held, target, elapsed) : target, r.fb.buffer);
    }
  }
}

// R15: raw writes draw unshifted and hold still.
void test_raw_writes_never_rotate() {
  Rig r(Surface::Base);
  r.write(rotatingScene(stopsA(), 5, RotationDir::Up), 0);
  for (int k = 0; k < 30; ++k) r.step();

  const std::vector<Color> raw = buildGradientWithStops(kPx, stopsB());
  r.cfg.beginFade(raw, 0);
  TEST_ASSERT_FALSE(r.cfg.rotating());
  for (int k = 0; k < 200; ++k) {
    r.step();
    assertFrame(raw, r.fb.buffer);
  }

  r.write(rotatingScene(stopsA(), 5, RotationDir::Up), 0);
  r.step();
  const Color mood(0x44, 0x00, 0xAA, 0x10);
  r.cfg.setSolid(mood);
  TEST_ASSERT_FALSE(r.cfg.rotating());
  for (int k = 0; k < 200; ++k) {
    r.step();
    assertFrame(std::vector<Color>(kPx, mood), r.fb.buffer);
  }
}

// R16: the zone marker holds still; release resumes from where the role was.
void test_zone_marker_holds_and_rotation_resumes() {
  Rig r(Surface::Shade);
  const RoleScene scene = rotatingScene(stopsA(), 5, RotationDir::Up);
  r.write(scene, 0);
  r.rotate(scene, 180);
  std::vector<Color> marker = scene.colors;
  for (size_t i = 10; i < 20; ++i) marker[i] = Color(255, 255, 255, 255);
  r.cfg.beginFade(marker, 0);
  r.cfg.lastWebSocketUpdateTimeMs = r.now;
  for (int k = 0; k < 120; ++k) {
    r.step();
    assertFrame(marker, r.fb.buffer);
  }
  r.cfg.showScene(scene, 0);
  for (int k = 0; k < 100; ++k) {
    r.step();
    assertFrame(r.expect(scene, r.now), r.fb.buffer);
  }
}

// R18: a rotating saved scene rotates from boot, from the frame the fade-in
// ends on, and never stops.
void test_saved_rotating_scene_rotates_from_boot() {
  Rig r(Surface::Base);
  const RoleScene saved = rotatingScene(stopsA(), 40, RotationDir::Up);
  r.saveScene(saved);
  r.cfg.colors = r.fb.defaultColors;
  r.fb.buffer = r.fb.defaultColors;
  r.tick(3016);
  TEST_ASSERT_EQUAL(PLAYING, r.cfg.animationState);
  assertFrame(saved.colors, r.fb.buffer);
  r.expect(saved, r.now);
  const uint32_t start = r.now;
  while (r.now - start < 300000) {
    r.step();
    TEST_ASSERT_EQUAL(PLAYING, r.cfg.animationState);
    const auto target = r.expect(saved, r.now);
    const uint32_t elapsed = r.now - start;
    assertFrame(elapsed < kDefaultFadeMs ? faded(saved.colors, target, elapsed) : target,
                r.fb.buffer);
  }
}

// R19, rotating saved scene: an abandoned preview crossfades back to it and it
// keeps rotating; when it already shows, no pixel changes.
void test_abandoned_preview_returns_to_the_rotating_saved_scene() {
  for (bool same : {false, true}) {
    Rig r(Surface::Shade);
    const RoleScene saved = rotatingScene(stopsA(), 40, RotationDir::Up);
    r.saveScene(saved);
    r.cfg.colors = saved.colors;
    r.tick(5000);
    r.expect(saved, r.now);
    r.rotate(saved, 20);
    const RoleScene preview = same ? saved : rotatingScene(stopsB(), 5, RotationDir::Down);
    r.write(preview, 0);
    const uint32_t edited = r.now;
    std::vector<Color> held;
    while (r.now + 16 <= edited + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS) {
      r.step();
      held = r.expect(preview, r.now);
      assertFrame(held, r.fb.buffer);
      TEST_ASSERT_NOT_EQUAL(STOPPED, r.cfg.animationState);
    }
    // The tick past the timeout installs the saved scene and draws its first
    // crossfade frame, which is the frame already on show.
    r.step();
    const uint32_t start = r.now;
    const auto first = r.expect(saved, r.now);
    assertFrame(same ? first : held, r.fb.buffer);
    while (r.now - start < 120000) {
      r.step();
      TEST_ASSERT_EQUAL(PLAYING, r.cfg.animationState);
      const auto target = r.expect(saved, r.now);
      const uint32_t elapsed = r.now - start;
      assertFrame(same || elapsed >= kDefaultFadeMs ? target : faded(held, target, elapsed),
                  r.fb.buffer);
    }
  }
}

// R19, static saved scene: the rotating preview crossfades to it, then the
// configurator runs on to STOPPED with the saved frame showing.
void test_abandoned_rotating_preview_settles_on_the_static_saved_scene() {
  Rig r(Surface::Base);
  const RoleScene preview = rotatingScene(stopsA(), 5, RotationDir::Up);
  r.write(preview, 0);
  const uint32_t edited = r.now;
  std::vector<Color> held;
  while (r.now + 16 <= edited + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS) {
    r.step();
    held = r.expect(preview, r.now);
  }
  r.step();
  const uint32_t start = r.now;
  assertFrame(held, r.fb.buffer);
  int savedFramesBeforeStop = 0;
  while (r.cfg.animationState != STOPPED) {
    r.step();
    const uint32_t elapsed = r.now - start;
    TEST_ASSERT_TRUE(elapsed < 3000);
    assertFrame(elapsed < kDefaultFadeMs ? faded(held, r.fb.defaultColors, elapsed)
                                         : r.fb.defaultColors,
                r.fb.buffer);
    if (elapsed >= kDefaultFadeMs) ++savedFramesBeforeStop;
  }
  TEST_ASSERT_FALSE(r.cfg.rotating());
  TEST_ASSERT_TRUE(savedFramesBeforeStop > 1);
  r.step();
  TEST_ASSERT_EQUAL(STOPPED, r.cfg.animationState);
}

// R20: knockout dims its pixels in place while the colours move under them,
// and the configurator never reads the dimmed frame back.
void test_knocked_out_pixels_stay_put() {
  Rig r(Surface::Base);
  KnockoutBehavior knockout(&r.fb, 0, true);
  knockout.knockoutPixels.assign(kPx, 100);
  for (size_t i : {0, 1, 2, 20}) knockout.knockoutPixels[i] = 0;
  for (size_t i : {30, 31}) knockout.knockoutPixels[i] = 40;
  const RoleScene scene = rotatingScene(stopsA(), 5, RotationDir::Up);
  r.write(scene, 0);
  std::vector<Color> first;
  bool moved = false;
  std::vector<Color> undimmed;
  for (int k = 0; k < 300; ++k) {
    r.step();
    undimmed = r.expect(scene, r.now);
    assertFrame(undimmed, r.fb.buffer);
    knockout.draw();
    for (size_t i = 0; i < kPx; ++i) {
      assertColor(setColorBrightness(undimmed[i], knockout.knockoutPixels[i]), r.fb.buffer[i], i);
    }
    if (first.empty()) first = r.fb.buffer;
    moved |= !(r.fb.buffer[10] == first[10]);
    for (size_t i : {0, 1, 2, 20}) assertColor(Color(), r.fb.buffer[i], i);
  }
  TEST_ASSERT_TRUE(moved);
  r.write(rotatingScene(stopsB(), 5, RotationDir::Up), kDefaultFadeMs);
  r.tick(r.now);
  assertFrame(undimmed, r.fb.buffer);
}

static constexpr uint32_t kPerMs5 = static_cast<uint32_t>((uint64_t{1} << 32) / 5000);

// R21: quiet entry's reset puts the next frame at the settled position,
// moved by at most one kMaxRotationStepMs step.
void test_reset_resumes_from_the_settled_frame() {
  Rig r(Surface::Shade);
  const RoleScene saved = rotatingScene(stopsA(), 5, RotationDir::Up);
  r.saveScene(saved);
  r.cfg.colors = saved.colors;
  r.tick(5000);
  r.expect(saved, r.now);
  r.rotate(saved, 140);
  TEST_ASSERT_NOT_EQUAL(0, r.mirror.phase());

  r.cfg.resetRotation();
  r.mirror.reset();
  r.step();
  assertFrame(r.expect(saved, r.now), r.fb.buffer);
  TEST_ASSERT_EQUAL_HEX32(0u - 16 * kPerMs5, r.mirror.phase());

  r.rotate(saved, 60);
  r.cfg.resetRotation();
  r.mirror.reset();
  r.tick(r.now + 10000);
  const auto settled = r.expect(saved, r.now);
  assertFrame(settled, r.fb.buffer);
  TEST_ASSERT_EQUAL_HEX32(0u - kMaxRotationStepMs * kPerMs5, r.mirror.phase());

  // Raw content on show at quiet entry resumes from the settled position too.
  r.rotate(saved, 60);
  r.cfg.beginFade(std::vector<Color>(kPx, Color(9, 9, 9, 9)), 0);
  r.cfg.lastWebSocketUpdateTimeMs = r.now;
  r.cfg.resetRotation();
  r.mirror.reset();
  for (int k = 0; k < 30; ++k) r.step();
  r.cfg.showScene(saved, 0);
  r.step();
  assertFrame(r.expect(saved, r.now), r.fb.buffer);
  TEST_ASSERT_EQUAL_HEX32(0u - kMaxRotationStepMs * kPerMs5, r.mirror.phase());
}

// R22: home mode with social off and every expression type disabled still
// runs a rotating configurator.
void test_home_mode_keeps_rotation_running() {
  const std::vector<std::string> everyType = {"bloom", "breathing", "flicker", "glitchy",
                                              "pulse", "shifty",    "spotty"};
  for (Surface s : {Surface::Shade, Surface::Base}) {
    Rig r(s);
    r.write(rotatingScene(stopsA(), 5, RotationDir::Up), 0);
    r.step();
    TEST_ASSERT_TRUE(r.cfg.rotating());
    const AnimatedBehavior& b = r.cfg;
    const HomeModeTick tick =
        homeModeTick(true, true, b.isSocialBehavior(), b.playsAppGreeting(),
                     homeModeExpressionSkips(b.homeModeExpressionId(), everyType));
    TEST_ASSERT_TRUE(tick == HomeModeTick::Run);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_frame_is_the_shifted_loop);
  RUN_TEST(test_up_rises_on_both_surfaces);
  RUN_TEST(test_one_stop_scene_is_solid_and_still);
  RUN_TEST(test_speed_and_direction_change_keep_the_position);
  RUN_TEST(test_rotating_draw_allocates_nothing);
  RUN_TEST(test_changes_crossfade_from_the_drawn_frame);
  RUN_TEST(test_raw_writes_never_rotate);
  RUN_TEST(test_zone_marker_holds_and_rotation_resumes);
  RUN_TEST(test_saved_rotating_scene_rotates_from_boot);
  RUN_TEST(test_abandoned_preview_returns_to_the_rotating_saved_scene);
  RUN_TEST(test_abandoned_rotating_preview_settles_on_the_static_saved_scene);
  RUN_TEST(test_knocked_out_pixels_stay_put);
  RUN_TEST(test_reset_resumes_from_the_settled_frame);
  RUN_TEST(test_home_mode_keeps_rotation_running);
  return UNITY_END();
}
