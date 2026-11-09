// Pins ConfiguratorBehavior frame by frame: each hash is FNV-1a over the play
// state, frame counter and buffer of the real configurator after every tick of
// a scripted sequence, recorded on the shipped code. A tick runs control() and
// then draw() unless STOPPED, as the compositor does.

#include <unity.h>

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

#include "../../src/behaviors/configurator.cpp"
#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/color_rotation.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"

using namespace lamp;

void setUp() {}
void tearDown() {}

static constexpr uint8_t kPx = 35;
static constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ULL;

// Per-pixel colours with every channel rising on some pixels and falling on
// others, so fades run both directions of the ease arithmetic.
static std::vector<Color> pattern(uint8_t seed) {
  std::vector<Color> out(kPx);
  for (uint8_t i = 0; i < kPx; ++i) {
    out[i] = Color(static_cast<uint8_t>(seed + i * 7), static_cast<uint8_t>(255 - seed - i * 5),
                   static_cast<uint8_t>((seed * 3 + i * 31) & 0xFF),
                   static_cast<uint8_t>(128 + seed + (i % 5) * 20));
  }
  return out;
}

struct Rig {
  FrameBuffer fb;
  ConfiguratorBehavior cfg;
  uint64_t hash = kFnvBasis;

  Rig() {
    fb.pixelCount = kPx;
    fb.buffer.assign(kPx, Color());
    fb.defaultColors = pattern(200);
    cfg.fb = &fb;
    cfg.frames = 120;
  }

  void mix(uint8_t b) { hash = (hash ^ b) * 0x100000001b3ULL; }
  void mix32(uint32_t v) {
    for (int s = 0; s < 32; s += 8) mix(static_cast<uint8_t>(v >> s));
  }

  void tick(uint32_t ms) {
    set_mock_millis(ms);
    cfg.control();
    const bool drawn = cfg.animationState != STOPPED;
    if (drawn) cfg.draw();
    mix(drawn);
    mix(static_cast<uint8_t>(cfg.animationState));
    mix32(cfg.frame);
    for (const Color& c : fb.buffer) {
      mix(c.r);
      mix(c.g);
      mix(c.b);
      mix(c.w);
    }
  }

  // Ticks from `from` (exclusive) to `to` (inclusive) on an uneven cadence.
  uint32_t run(uint32_t from, uint32_t to) {
    static constexpr uint32_t kSteps[] = {16, 17, 16, 23, 16, 41, 16, 19};
    uint32_t t = from;
    size_t k = 0;
    while (t + kSteps[k % 8] <= to) {
      t += kSteps[k++ % 8];
      tick(t);
    }
    return t;
  }

  void write(const std::vector<Color>& target, uint32_t fadeMs, uint32_t ms) {
    set_mock_millis(ms);
    cfg.beginFade(target, fadeMs);
    cfg.lastWebSocketUpdateTimeMs = ms;
  }
};

static void expectHash(uint64_t expected, uint64_t actual) {
  char msg[48];
  std::snprintf(msg, sizeof msg, "actual 0x%016" PRIx64, actual);
  TEST_ASSERT_EQUAL_HEX64_MESSAGE(expected, actual, msg);
}

void test_cold_start() {
  Rig r;
  r.fb.buffer = pattern(9);
  r.write(pattern(60), kDefaultFadeMs, 5000);
  r.run(5000, 6500);
  expectHash(0xd805719c7b317ffdULL, r.hash);
}

void test_fresh_fade() {
  Rig r;
  r.fb.buffer = pattern(9);
  r.cfg.colors = pattern(23);
  r.write(pattern(60), kDefaultFadeMs, 5000);
  r.run(5000, 6500);
  expectHash(0x37d4c4c282a34199ULL, r.hash);
}

void test_mid_fade_interrupt() {
  Rig r;
  r.cfg.colors = pattern(9);
  r.write(pattern(60), kDefaultFadeMs, 5000);
  const uint32_t t = r.run(5000, 5049);
  r.write(pattern(140), kDefaultFadeMs, t + 3);
  r.run(t + 3, t + 60);
  r.write(pattern(30), 250, t + 61);
  r.run(t + 61, 7000);
  expectHash(0xa5b7ad07f99ec944ULL, r.hash);
}

void test_set_solid() {
  Rig r;
  r.cfg.colors = pattern(9);
  r.write(pattern(60), kDefaultFadeMs, 5000);
  uint32_t t = r.run(5000, 5170);
  for (const Color c : {Color(255, 10, 0, 40), Color(3, 200, 90, 255), Color(0, 0, 0, 0)}) {
    set_mock_millis(t + 5);
    r.cfg.setSolid(c);
    t = r.run(t + 5, t + 300);
  }
  r.write(pattern(77), kDefaultFadeMs, t + 2);
  r.run(t + 2, t + 1500);
  expectHash(0x32a28bf6ea99be94ULL, r.hash);
}

void test_idle_run_to_stopped() {
  Rig r;
  r.cfg.colors = pattern(9);
  r.write(pattern(60), kDefaultFadeMs, 1000);
  uint32_t t = r.run(1000, 20000);
  r.write(pattern(110), kDefaultFadeMs, t + 4);
  t = r.run(t + 4, 83000);
  TEST_ASSERT_EQUAL(STOPPED, r.cfg.animationState);
  expectHash(0x02735f5d35bcc59eULL, r.hash);
}

void test_boot_path() {
  Rig r;
  r.cfg.colors = r.fb.defaultColors;
  r.fb.buffer = r.fb.defaultColors;
  uint32_t t = r.run(3000, 5000);
  TEST_ASSERT_EQUAL(STOPPED, r.cfg.animationState);
  r.write(pattern(60), kDefaultFadeMs, t + 1);
  r.run(t + 1, 8000);
  expectHash(0xe9200cbc0ccd6c95ULL, r.hash);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cold_start);
  RUN_TEST(test_fresh_fade);
  RUN_TEST(test_mid_fade_interrupt);
  RUN_TEST(test_set_solid);
  RUN_TEST(test_idle_run_to_stopped);
  RUN_TEST(test_boot_path);
  return UNITY_END();
}
