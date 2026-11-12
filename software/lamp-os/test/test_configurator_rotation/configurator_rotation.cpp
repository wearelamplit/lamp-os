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
#include "../../src/behaviors/zone_preview_surface.cpp"
#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/color_rotation.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"
#include "../../src/util/levels.cpp"
#include "core/home_mode_gate.hpp"
#include "../replaced_new/replaced_new.hpp"

static size_t g_allocs = 0;

void* testAllocate(size_t n) {
  ++g_allocs;
  return std::malloc(n);
}
void testRelease(void* p) noexcept { std::free(p); }

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
  // The expected clock, advanced on every rotating draw as the configurator's is.
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

// Each surface and direction draws the loop shifted by its clock.
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

// Shade index 0 is its bottom and base index 0 its top, so Up
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

// One stop never rotates.
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

// A change of loop length or direction keeps the frame and the phase.
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

// Every change crossfades over kDefaultFadeMs from the frame on show.
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

// A write that lands while a crossfade runs, as a slider drag does.
// The next crossfade starts from the frame on show mid-fade.
void test_change_mid_crossfade_starts_from_the_drawn_frame() {
  Rig r(Surface::Shade);
  const RoleScene first = rotatingScene(stopsA(), 5, RotationDir::Up);
  r.write(first, 0);
  r.rotate(first, 60);
  const RoleScene drag = rotatingScene(stopsB(), 5, RotationDir::Up);
  r.write(drag, kDefaultFadeMs);
  r.tick(r.now);
  r.expect(drag, r.now);
  r.step(23);
  r.expect(drag, r.now);
  r.step(23);
  r.expect(drag, r.now);
  const std::vector<Color> held = r.fb.buffer;
  const RoleScene next = rotatingScene(stopsA(), 9, RotationDir::Down);
  r.write(next, kDefaultFadeMs);
  const uint32_t start = r.now;
  r.tick(r.now);
  assertFrame(held, r.fb.buffer);
  r.expect(next, r.now);
  while (r.now - start < 160) {
    r.step();
    const auto target = r.expect(next, r.now);
    const uint32_t elapsed = r.now - start;
    assertFrame(elapsed < kDefaultFadeMs ? faded(held, target, elapsed) : target, r.fb.buffer);
  }
}

// Raw writes draw unshifted and hold still.
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
  TEST_ASSERT_FALSE(r.cfg.scene().rotation.on);
  for (int k = 0; k < 200; ++k) {
    r.step();
    assertFrame(std::vector<Color>(kPx, mood), r.fb.buffer);
  }
}

// The zone marker holds still. Release resumes from where the role was.
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

// An abandoned edit leaves the lamp on its saved scene while RAM config keeps
// the edit. A Zone marker put up after that releases to the saved scene on
// show, rotating on from where the marker stopped it, or the static saved frame.
void test_zone_release_restores_the_scene_shown_after_an_abandoned_edit() {
  const RoleScene edit = rotatingScene(stopsB(), 5, RotationDir::Down);
  for (bool savedRotates : {true, false}) {
    Rig r(Surface::Shade);
    const RoleScene saved = savedRotates ? rotatingScene(stopsA(), 40, RotationDir::Up)
                                         : buildRoleScene(kPx, stopsA(), ColorRotation{});
    r.saveScene(saved);
    r.cfg.colors = saved.colors;
    r.tick(5000);
    if (savedRotates) r.expect(saved, r.now);
    r.rotate(saved, 20);
    r.write(edit, 0);
    const uint32_t edited = r.now;
    while (r.now <= edited + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS + 1000) {
      r.step();
      r.expect(r.now <= edited + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS ? edit : saved, r.now);
    }
    const std::vector<Color> shown = r.fb.buffer;
    TEST_ASSERT_EQUAL(savedRotates, r.cfg.rotating());

    ZonePreviewSurface marker;
    const Color white(255, 255, 255, 255);
    for (uint32_t pos = 4; pos < 12; ++pos) {
      set_mock_millis(r.now);
      marker.show(r.cfg, kPx, pos, pos + 9, white);
      r.cfg.lastWebSocketUpdateTimeMs = r.now;
      r.step();
      assertFrame(buildZonePreviewBuffer(kPx, pos, pos + 9, white), r.fb.buffer);
    }
    set_mock_millis(r.now);
    marker.release(r.cfg);
    TEST_ASSERT_FALSE(marker.active());
    TEST_ASSERT_EQUAL(savedRotates, r.cfg.rotating());
    for (int k = 0; k < 200; ++k) {
      r.step();
      assertFrame(savedRotates ? r.expect(saved, r.now) : shown, r.fb.buffer);
    }
  }
}

// A marker that goes up mid-edit snapshots the edit, not the saved scene.
void test_zone_release_restores_the_edit_when_the_marker_goes_up_mid_edit() {
  Rig r(Surface::Shade);
  const RoleScene saved = buildRoleScene(kPx, stopsA(), ColorRotation{});
  const RoleScene edit = rotatingScene(stopsB(), 5, RotationDir::Down);
  r.saveScene(saved);
  r.cfg.colors = saved.colors;
  r.tick(5000);
  r.write(edit, 0);
  r.rotate(edit, 20);

  ZonePreviewSurface marker;
  const Color white(255, 255, 255, 255);
  for (uint32_t pos = 4; pos < 8; ++pos) {
    set_mock_millis(r.now);
    marker.show(r.cfg, kPx, pos, pos + 9, white);
    r.cfg.lastWebSocketUpdateTimeMs = r.now;
    r.step();
    assertFrame(buildZonePreviewBuffer(kPx, pos, pos + 9, white), r.fb.buffer);
  }
  set_mock_millis(r.now);
  marker.release(r.cfg);
  TEST_ASSERT_TRUE(r.cfg.rotating());
  for (int k = 0; k < 200; ++k) {
    r.step();
    assertFrame(r.expect(edit, r.now), r.fb.buffer);
  }
}

// Releasing with no marker up leaves the configurator as it was.
void test_zone_release_without_a_marker_changes_nothing() {
  Rig r(Surface::Shade);
  const RoleScene scene = rotatingScene(stopsA(), 5, RotationDir::Up);
  r.write(scene, 0);
  r.rotate(scene, 20);
  ZonePreviewSurface marker;
  set_mock_millis(r.now);
  marker.release(r.cfg);
  TEST_ASSERT_FALSE(marker.active());
  TEST_ASSERT_TRUE(r.cfg.rotating());
  TEST_ASSERT_EQUAL(kPx, r.cfg.colors.size());
  r.rotate(scene, 100);
}

// A rotating saved scene rotates from boot, from the frame the fade-in
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

// With a rotating saved scene, an abandoned preview crossfades back to it and it
// keeps rotating, whichever of colours, loop length and direction the preview
// changed. When it already shows, no pixel changes.
void test_abandoned_preview_returns_to_the_rotating_saved_scene() {
  const RoleScene saved = rotatingScene(stopsA(), 40, RotationDir::Up);
  const RoleScene previews[] = {
      rotatingScene(stopsB(), 5, RotationDir::Down),   // everything
      rotatingScene(stopsB(), 40, RotationDir::Up),    // colours
      rotatingScene(stopsA(), 5, RotationDir::Up),     // loop length
      rotatingScene(stopsA(), 40, RotationDir::Down),  // direction
      saved,
  };
  for (const RoleScene& preview : previews) {
    const bool same = preview.colors == saved.colors && preview.rotation == saved.rotation;
    Rig r(Surface::Shade);
    r.saveScene(saved);
    r.cfg.colors = saved.colors;
    r.tick(5000);
    r.expect(saved, r.now);
    r.rotate(saved, 20);
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

// With a static saved scene, the rotating preview crossfades to it, then the
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

// Run-out to STOPPED waits for the crossfade however fast the configurator
// ticks and however few frames it counts.
void test_static_saved_fallback_stops_only_after_the_crossfade() {
  struct Case {
    uint32_t dt, frames;
  };
  for (const Case& c : {Case{1, 120}, Case{16, 62}}) {
    Rig r(Surface::Base);
    r.cfg.frames = c.frames;
    const RoleScene preview = rotatingScene(stopsA(), 5, RotationDir::Up);
    r.write(preview, 0);
    const uint32_t edited = r.now;
    std::vector<Color> held;
    while (r.now + c.dt <= edited + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS) {
      r.step(c.dt);
      held = r.expect(preview, r.now);
    }
    r.step(c.dt);
    const uint32_t start = r.now;
    assertFrame(held, r.fb.buffer);
    while (r.cfg.animationState != STOPPED) {
      r.step(c.dt);
      const uint32_t elapsed = r.now - start;
      TEST_ASSERT_TRUE(elapsed < 3000);
      assertFrame(elapsed < kDefaultFadeMs ? faded(held, r.fb.defaultColors, elapsed)
                                           : r.fb.defaultColors,
                  r.fb.buffer);
    }
    assertFrame(r.fb.defaultColors, r.fb.buffer);
  }
}

// A raw write and a static saved scene landing while a rotating saved scene
// plays put the configurator on the static path. It matches one that never
// rotated, through the timeout to STOPPED.
void test_rotation_off_while_playing_takes_the_static_path() {
  Rig a(Surface::Shade);
  a.saveScene(rotatingScene(stopsA(), 40, RotationDir::Up));
  a.cfg.colors = a.fb.defaultColors;
  a.tick(5000);
  for (int k = 0; k < 37; ++k) a.step();
  TEST_ASSERT_EQUAL(PLAYING, a.cfg.animationState);

  Rig b(Surface::Shade);
  b.cfg.colors = b.fb.defaultColors;
  b.tick(a.now);
  TEST_ASSERT_EQUAL(STOPPED, b.cfg.animationState);

  const uint32_t edited = a.now;
  const std::vector<Color> raw = buildGradientWithStops(kPx, rgb());
  for (Rig* x : {&a, &b}) {
    x->cfg.beginFade(raw, 0);
    x->cfg.lastWebSocketUpdateTimeMs = edited;
  }
  a.fb.defaultColors = b.fb.defaultColors;
  a.fb.defaultRotation = ColorRotation{};

  bool twinPaused = false;
  while (a.cfg.animationState != STOPPED || b.cfg.animationState != STOPPED) {
    a.step();
    b.tick(a.now);
    TEST_ASSERT_TRUE(a.now < edited + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS + 5000);
    TEST_ASSERT_FALSE(a.cfg.rotating());
    assertFrame(raw, a.fb.buffer);
    twinPaused |= b.cfg.animationState == PAUSED;
    if (twinPaused) {
      TEST_ASSERT_EQUAL(b.cfg.animationState, a.cfg.animationState);
      TEST_ASSERT_EQUAL_UINT32(b.cfg.frame, a.cfg.frame);
    }
  }
  TEST_ASSERT_TRUE(a.now > edited + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS);
}

// A rotating scene built for another pixel count, or for none, draws unrotated.
void test_scene_for_another_strip_never_rotates() {
  const ColorRotation on{true, 5, RotationDir::Up};
  for (int px : {kPx - 1, kPx + 1, int{kPx}}) {
    Rig r(Surface::Shade);
    const RoleScene scene = buildRoleScene(static_cast<uint8_t>(px), stopsA(), on);
    TEST_ASSERT_TRUE(scene.rotation.on);
    r.cfg.showScene(scene, 0);
    TEST_ASSERT_EQUAL(px == kPx, r.cfg.rotating());
  }
  Rig r(Surface::Shade);
  r.fb.pixelCount = 0;
  r.fb.buffer.clear();
  const RoleScene empty = buildRoleScene(0, stopsA(), on);
  TEST_ASSERT_TRUE(empty.rotation.on);
  TEST_ASSERT_EQUAL_size_t(0, empty.colors.size());
  r.cfg.showScene(empty, 0);
  TEST_ASSERT_FALSE(r.cfg.rotating());
}

// Knockout dims its pixels in place while the colours move under them,
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

// Quiet entry's reset puts the next frame at the settled position,
// moved by at most one lampos::kMaxFrameDeltaMs step.
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
  TEST_ASSERT_EQUAL_HEX32(0u - lampos::kMaxFrameDeltaMs * kPerMs5, r.mirror.phase());

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
  TEST_ASSERT_EQUAL_HEX32(0u - lampos::kMaxFrameDeltaMs * kPerMs5, r.mirror.phase());
}

// Home mode with social off and every expression type disabled still
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

// A static edit that ran its crossfade and edit window, then a whole wrap with
// no other: the configurator stays settled on the edited frame, with neither
// the edit window nor the crossfade coming back.
void test_a_settled_edit_stays_settled_a_wrap_later() {
  Rig r(Surface::Shade);
  r.tick(r.now);
  const RoleScene edit = buildRoleScene(kPx, stopsA(), ColorRotation{});
  r.write(edit, kDefaultFadeMs);
  const uint32_t edited = r.now;
  while (r.now < edited + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS + 1000) r.step();
  TEST_ASSERT_EQUAL(STOPPED, r.cfg.animationState);
  const std::vector<Color> settled = r.fb.buffer;
  for (uint64_t t = r.now; t < edited + (1ull << 32); t += 1ull << 28) {
    r.tick(static_cast<uint32_t>(t));
  }
  r.tick(edited + 50);
  TEST_ASSERT_EQUAL(STOPPED, r.cfg.animationState);
  assertFrame(settled, r.fb.buffer);
}

// A rotating scene's crossfade, ended, must not replay a wrap later.
void test_a_finished_crossfade_does_not_replay_a_wrap_later() {
  Rig r(Surface::Shade);
  const RoleScene first = rotatingScene(stopsA(), 40, RotationDir::Up);
  r.saveScene(first);
  r.write(first, 0);
  r.rotate(first, 30);
  const RoleScene second = rotatingScene(stopsB(), 40, RotationDir::Up);
  r.saveScene(second);
  r.write(second, kDefaultFadeMs);
  const uint32_t faded = r.now;
  while (r.now < faded + CONFIGURATOR_WEBSOCKET_TIMEOUT_MS + 1000) {
    r.step();
    r.expect(second, r.now);
  }
  for (uint64_t t = r.now; t < faded + (1ull << 32); t += 1ull << 28) {
    r.tick(static_cast<uint32_t>(t));
    r.expect(second, r.now);
  }
  r.tick(faded + 50);
  assertFrame(r.expect(second, r.now), r.fb.buffer);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_frame_is_the_shifted_loop);
  RUN_TEST(test_a_settled_edit_stays_settled_a_wrap_later);
  RUN_TEST(test_a_finished_crossfade_does_not_replay_a_wrap_later);
  RUN_TEST(test_up_rises_on_both_surfaces);
  RUN_TEST(test_one_stop_scene_is_solid_and_still);
  RUN_TEST(test_speed_and_direction_change_keep_the_position);
  RUN_TEST(test_rotating_draw_allocates_nothing);
  RUN_TEST(test_changes_crossfade_from_the_drawn_frame);
  RUN_TEST(test_change_mid_crossfade_starts_from_the_drawn_frame);
  RUN_TEST(test_raw_writes_never_rotate);
  RUN_TEST(test_zone_marker_holds_and_rotation_resumes);
  RUN_TEST(test_zone_release_restores_the_scene_shown_after_an_abandoned_edit);
  RUN_TEST(test_zone_release_restores_the_edit_when_the_marker_goes_up_mid_edit);
  RUN_TEST(test_zone_release_without_a_marker_changes_nothing);
  RUN_TEST(test_saved_rotating_scene_rotates_from_boot);
  RUN_TEST(test_abandoned_preview_returns_to_the_rotating_saved_scene);
  RUN_TEST(test_abandoned_rotating_preview_settles_on_the_static_saved_scene);
  RUN_TEST(test_static_saved_fallback_stops_only_after_the_crossfade);
  RUN_TEST(test_rotation_off_while_playing_takes_the_static_path);
  RUN_TEST(test_scene_for_another_strip_never_rotates);
  RUN_TEST(test_knocked_out_pixels_stay_put);
  RUN_TEST(test_reset_resumes_from_the_settled_frame);
  RUN_TEST(test_home_mode_keeps_rotation_running);
  return UNITY_END();
}
