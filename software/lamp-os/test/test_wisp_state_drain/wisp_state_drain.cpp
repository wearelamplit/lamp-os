// Compositor::applyWispStateSlot, the drain's STATE-slot step: this lamp's
// RGBW entry lands as the wisp target with W intact, black included, and an
// absent entry reports unpainted. Links against a stub Compositor constructor.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/network/mesh/pending_slots.cpp"
#include "core/compositor.hpp"

namespace lp = lamp_protocol;
using lamp::Color;
using lamp::Compositor;
using lamp::PendingWispState;

namespace lamp {
Compositor::Compositor()
    : wispShadePresence_(1.0f, 0.0f), wispBasePresence_(1.0f, 0.0f) {}
}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

namespace {
const uint8_t kSrc[6]   = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
const uint8_t kMine[6]  = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
const uint8_t kOther[6] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25};

PendingWispState gSlot;

// kOther at index 0, kMine at index 1, each with its own RGB and W.
void fillTwo(const uint8_t mineRgb[6], const uint8_t w[4], bool withW) {
  uint8_t entries[2 * lp::WISP_STATE_ENTRY_SIZE];
  std::memcpy(entries, kOther, 6);
  const uint8_t otherRgb[6] = {7, 8, 9, 10, 11, 12};
  std::memcpy(entries + 6, otherRgb, 6);
  std::memcpy(entries + 12, kMine, 6);
  std::memcpy(entries + 18, mineRgb, 6);
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 100, 60000, 0,
                                      entries, withW ? w : nullptr, 2);
  lp::ParsedWispState ws;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, ws));
  lamp::fillPendingWispState(gSlot, ws);
}

void assertColor(Color want, Color got) {
  TEST_ASSERT_EQUAL_UINT8(want.r, got.r);
  TEST_ASSERT_EQUAL_UINT8(want.g, got.g);
  TEST_ASSERT_EQUAL_UINT8(want.b, got.b);
  TEST_ASSERT_EQUAL_UINT8(want.w, got.w);
}
}  // namespace

void test_drain_applies_own_rgbw() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  assertColor(Color(40, 50, 60, 0xC8), c.wispStateBaseColor());
  assertColor(Color(70, 80, 90, 0x37), c.wispStateShadeColor());
}

// Full wisp presence replaces the own scene with the layers' eased target.
void assertLayers(const Compositor& c, Color wantBase, Color wantShade) {
  const Color own(200, 100, 50, 0);
  lamp::LayerStack base = c.wispLayer(true);
  lamp::LayerStack shade = c.wispLayer(false);
  for (int i = 0; i < 600; ++i) {
    base.tick(1.0f);
    shade.tick(1.0f);
  }
  assertColor(wantBase, base.composite(own));
  assertColor(wantShade, shade.composite(own));
}

void test_drain_feeds_render_layers_rgbw() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  assertLayers(c, Color(40, 50, 60, 0xC8), Color(70, 80, 90, 0x37));
}

void test_drain_w_only_repaint_reaches_layers() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w1[4] = {0, 0, 0xC8, 0x37};
  const uint8_t w2[4] = {0, 0, 0x21, 0x9A};
  Compositor c;
  fillTwo(rgb, w1, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  fillTwo(rgb, w2, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 2000));
  assertLayers(c, Color(40, 50, 60, 0x21), Color(70, 80, 90, 0x9A));
}

void test_drain_applies_warm_white_only() {
  const uint8_t rgb[6] = {0, 0, 0, 0, 0, 0};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  assertColor(Color(0, 0, 0, 0xC8), c.wispStateBaseColor());
  assertColor(Color(0, 0, 0, 0x37), c.wispStateShadeColor());
}

void test_drain_applies_black() {
  const uint8_t rgb[6] = {0, 0, 0, 0, 0, 0};
  const uint8_t w[4] = {9, 9, 0, 0};
  Compositor c;
  // Painted first, so black must overwrite a live colour.
  const uint8_t seed[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t seedW[4] = {0, 0, 7, 7};
  fillTwo(seed, seedW, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  fillTwo(rgb, w, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 2000));
  assertColor(Color(0, 0, 0, 0), c.wispStateBaseColor());
  assertColor(Color(0, 0, 0, 0), c.wispStateShadeColor());
}

void test_drain_without_w_plane_applies_w_zero() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};
  Compositor c;
  fillTwo(rgb, w, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  fillTwo(rgb, w, false);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 2000));
  assertColor(Color(40, 50, 60, 0), c.wispStateBaseColor());
  assertColor(Color(70, 80, 90, 0), c.wispStateShadeColor());
}

// A wisp that paints this lamp then goes silent ages out after
// kWispStateFreshMs. A whole millis() wrap later, with no STATE since, its
// paint must not read fresh again.
void test_silent_wisp_stays_aged_out_a_wrap_later() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0, 0, 0, 0};
  fillTwo(rgb, w, true);
  Compositor c;
  const uint32_t paintedMs = 1000;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, paintedMs));
  for (uint64_t t = paintedMs; t < paintedMs + (1ull << 32); t += 1ull << 28) {
    c.retireWispState(static_cast<uint32_t>(t));
  }
  TEST_ASSERT_FALSE(c.wispStateFresh(paintedMs + 10));
}

// Away from the wrap the latch changes nothing: still fresh inside the
// window, and a STATE after an age-out paints again.
void test_wisp_state_retire_keeps_a_fresh_paint() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0, 0, 0, 0};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  c.retireWispState(1000 + Compositor::kWispStateFreshMs - 1);
  TEST_ASSERT_TRUE(c.wispStateFresh(1000 + Compositor::kWispStateFreshMs - 1));
  c.retireWispState(1000 + Compositor::kWispStateFreshMs);
  TEST_ASSERT_FALSE(c.wispStateFresh(1000 + Compositor::kWispStateFreshMs));
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 90000));
  TEST_ASSERT_TRUE(c.wispStateFresh(90001));
}

void test_drain_absent_entry_reports_unpainted() {
  const uint8_t kStranger[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
  const uint8_t rgb[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t w[4] = {0, 0, 0, 0};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_FALSE(c.applyWispStateSlot(gSlot, kStranger, 1000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_drain_applies_own_rgbw);
  RUN_TEST(test_drain_feeds_render_layers_rgbw);
  RUN_TEST(test_drain_w_only_repaint_reaches_layers);
  RUN_TEST(test_drain_applies_warm_white_only);
  RUN_TEST(test_drain_applies_black);
  RUN_TEST(test_drain_without_w_plane_applies_w_zero);
  RUN_TEST(test_drain_absent_entry_reports_unpainted);
  RUN_TEST(test_silent_wisp_stays_aged_out_a_wrap_later);
  RUN_TEST(test_wisp_state_retire_keeps_a_fresh_paint);
  return UNITY_END();
}
