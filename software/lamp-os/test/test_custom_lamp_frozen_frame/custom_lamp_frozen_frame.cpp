// The first frame a custom lamp's own animation runs after weeks without one
// steps like a normal frame: the loaf ring's rotation and the staff's mood
// scrub. Two twins run the same frames, then one waits the longest normal
// frame (lampos::kMaxFrameDeltaMs) and the other three weeks; both must land
// in the same place.

#include <unity.h>

#include <cstdint>
#include <vector>

#include <lampos/clock.hpp>

#include "../../src/behaviors/configurator.cpp"
#include "../../src/components/input/button.cpp"
#include "../../src/components/input/touch.cpp"
#include "../../src/config/disposition_store.cpp"
#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/lamps/loaf/loaf_ring_behavior.cpp"
#include "../../src/lamps/staff/staff_input_behavior.cpp"
#include "../../src/lamps/staff/unlock_combo.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/color_rotation.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"
#include "../../src/util/heap_guard.cpp"

lamp::Config config;

namespace {
lamp::Color g_solid;
}  // namespace

namespace lamp {
void BehaviorContext::forEachNearby(const std::function<bool(const PeerView&)>&) {}
void BehaviorContext::setSolidColor(Color c) { g_solid = c; }
void BehaviorContext::setBrightness(uint8_t) {}
void BehaviorContext::setSurfaceBrightness(Surface, uint8_t) {}
}  // namespace lamp

using namespace lamp;

namespace {

constexpr uint32_t kFrameMs = 16;
constexpr uint32_t kStartMs = 1000;
constexpr uint32_t kThreeWeeksMs = 21u * 24 * 60 * 60 * 1000;
constexpr uint8_t kRingPixels = 40;
constexpr uint32_t kRevMs = 7000;

void assertSameFrame(const std::vector<Color>& want, const std::vector<Color>& got) {
  TEST_ASSERT_EQUAL_size_t(want.size(), got.size());
  for (size_t i = 0; i < want.size(); ++i) {
    TEST_ASSERT_EQUAL_HEX8(want[i].r, got[i].r);
    TEST_ASSERT_EQUAL_HEX8(want[i].g, got[i].g);
    TEST_ASSERT_EQUAL_HEX8(want[i].b, got[i].b);
    TEST_ASSERT_EQUAL_HEX8(want[i].w, got[i].w);
  }
}

struct Ring {
  FrameBuffer fb;
  loaf::LoafRingBehavior ring{&fb, /*isBase=*/true, kRevMs};
  Ring() { fb.pixelCount = kRingPixels; fb.buffer.assign(kRingPixels, Color()); }
  const std::vector<Color>& frame(uint32_t nowMs) {
    set_mock_millis(nowMs);
    ring.control();
    ring.draw();
    return fb.buffer;
  }
};

// A staff unlocked and mid mood scrub: stoke long-press and release, tap the
// top pad, then a second stoke long-press starts the scrub.
struct Staff {
  FrameBuffer fb;
  BehaviorContext ctx;
  Button stoke;
  Touch topPad;
  staff::StaffInputBehavior input{&fb, config, &stoke, &topPad, nullptr};
  uint32_t now = kStartMs;

  Staff() {
    input.setBehaviorContext(&ctx);
    for (int i = 0; i < 16; ++i) topPad.update(2000, step(kFrameMs));
    holdStoke(800);
    stoke.update(false, step(kFrameMs));
    stoke.update(false, step(40));
    topPad.update(1900, step(kFrameMs));
    topPad.update(1900, step(kFrameMs));
    topPad.update(2000, step(kFrameMs));
    holdStoke(800);
  }
  // The input callbacks read millis(), so the mock clock follows `now`.
  uint32_t step(uint32_t ms) {
    now += ms;
    set_mock_millis(now);
    return now;
  }
  void holdStoke(uint32_t forMs) {
    const uint32_t until = now + forMs;
    while (now < until) stoke.update(true, step(kFrameMs));
  }
  Color frame(uint32_t nowMs) {
    set_mock_millis(nowMs);
    g_solid = Color();
    input.control();
    return g_solid;
  }
};

}  // namespace

void setUp() {
  set_mock_millis(kStartMs);
  config.base.broadcastColors() = {Color(255, 0, 0, 0), Color(0, 0, 255, 0)};
  config.shade.broadcastColors() = {Color(255, 0, 0, 0)};
}
void tearDown() {}

void test_loaf_ring_resumes_one_frame_round() {
  Ring normal, frozen;
  uint32_t now = kStartMs;
  for (; now < kStartMs + 1500; now += kFrameMs) {
    assertSameFrame(normal.frame(now), frozen.frame(now));
  }
  const std::vector<Color> before = normal.fb.buffer;
  for (int i = 0; i < 3; ++i) {
    const uint32_t step = lampos::kMaxFrameDeltaMs + i * kFrameMs;
    assertSameFrame(normal.frame(now + step), frozen.frame(now + kThreeWeeksMs + step));
  }
  bool moved = false;
  for (size_t i = 0; i < before.size(); ++i) moved = moved || !(before[i] == normal.fb.buffer[i]);
  TEST_ASSERT_TRUE(moved);
}

void test_staff_mood_scrub_resumes_one_frame_on() {
  Staff normal, frozen;
  TEST_ASSERT_EQUAL_UINT32(normal.now, frozen.now);
  uint32_t now = normal.now;
  for (int i = 0; i < 20; ++i, now += kFrameMs) {
    const Color want = normal.frame(now);
    const Color got = frozen.frame(now);
    TEST_ASSERT_TRUE(want == got);
  }
  const Color before = normal.frame(now);
  frozen.frame(now);
  for (int i = 0; i < 3; ++i) {
    const uint32_t step = lampos::kMaxFrameDeltaMs + i * kFrameMs;
    const Color want = normal.frame(now + step);
    const Color got = frozen.frame(now + kThreeWeeksMs + step);
    TEST_ASSERT_EQUAL_HEX8(want.r, got.r);
    TEST_ASSERT_EQUAL_HEX8(want.g, got.g);
    TEST_ASSERT_EQUAL_HEX8(want.b, got.b);
  }
  TEST_ASSERT_FALSE(before == normal.frame(now + lampos::kMaxFrameDeltaMs + 3 * kFrameMs));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_loaf_ring_resumes_one_frame_round);
  RUN_TEST(test_staff_mood_scrub_resumes_one_frame_on);
  return UNITY_END();
}
