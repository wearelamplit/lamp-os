// Native-host unit tests for the OTA visual indicator paint() and its silent
// (FS OTA) sibling paintSilent().
//
// Mirrors the production shape inline (no firmwareReceiver /
// firmwareDistributor globals reachable from the native env). Observable
// contract: per-pixel output for a given (localBase, peerBase, progress)
// tuple, and that paint() is called on shade only so base stays untouched;
// paintSilent() settles shade and base independently with no dim/band/pulse.
//
// Production code: src/components/firmware/ota_indicator.{hpp,cpp}.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace test {

struct Color {
  uint8_t r = 0, g = 0, b = 0, w = 0;
  Color() = default;
  Color(uint8_t ir, uint8_t ig, uint8_t ib, uint8_t iw)
      : r(ir), g(ig), b(ib), w(iw) {}
  bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b && w == o.w;
  }
};

struct FrameBuffer {
  std::vector<Color> buffer;
  std::vector<Color> defaultColors;
};

constexpr uint8_t kDimScale255 = 128;  // gamma-corrected dim background; matches production

inline Color scaleColor(const Color& c, uint8_t num) {
  return Color(
      static_cast<uint8_t>((static_cast<uint16_t>(c.r) * num) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.g) * num) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.b) * num) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.w) * num) / 255));
}

// Mirror of ota_indicator::paint with the OTA session state passed
// explicitly (no firmwareReceiver / firmwareDistributor globals reachable
// from the native env). nowMs is accepted but only used by production debug
// logging; pixel output is deterministic without it.
void paint(FrameBuffer* fb, const Color& localBase, uint32_t /*nowMs*/,
           bool haveSession, const Color& peerBase,
           uint32_t done, uint32_t total) {
  if (!fb) return;
  const size_t pixelCount = fb->buffer.size();
  if (pixelCount == 0) return;

  const Color dim = scaleColor(localBase, kDimScale255);
  for (size_t i = 0; i < pixelCount; i++) fb->buffer[i] = dim;
  if (!haveSession || total == 0) return;

  const Color peerSolid = peerBase;
  if (done > total) done = total;
  const uint64_t scaledProgress =
      (static_cast<uint64_t>(pixelCount) * static_cast<uint64_t>(done) * 256u) /
      static_cast<uint64_t>(total);
  const size_t wholePixels = static_cast<size_t>(scaledProgress >> 8);
  const uint8_t fracEdge255 = static_cast<uint8_t>(scaledProgress & 0xFFu);

  for (size_t i = 0; i < wholePixels && i < pixelCount; i++) {
    fb->buffer[i] = peerSolid;
  }
  if (wholePixels < pixelCount) {
    const Color& a = dim;
    const Color& b = peerSolid;
    const uint8_t f = fracEdge255;
    const uint8_t inv = static_cast<uint8_t>(255u - f);
    fb->buffer[wholePixels] = Color(
        static_cast<uint8_t>(
            (static_cast<uint16_t>(a.r) * inv + static_cast<uint16_t>(b.r) * f) / 255),
        static_cast<uint8_t>(
            (static_cast<uint16_t>(a.g) * inv + static_cast<uint16_t>(b.g) * f) / 255),
        static_cast<uint8_t>(
            (static_cast<uint16_t>(a.b) * inv + static_cast<uint16_t>(b.b) * f) / 255),
        static_cast<uint8_t>(
            (static_cast<uint16_t>(a.w) * inv + static_cast<uint16_t>(b.w) * f) / 255));
  }
}

// ---------------------------------------------------------------------------
// Base-surface settle mirror. Constants must match production
// (src/components/firmware/ota_indicator.cpp).
// ---------------------------------------------------------------------------

constexpr uint32_t kBaseSettleDurationMs = 600;

// Test double for lamp::fade's boundary behavior: exact at currentStep==0
// (start) and currentStep>=duration (end), linear in between. The quadratic
// curve shape itself is pinned by test_fade; paintBase only needs correct
// start/end boundaries plus "moves toward end" in between.
inline Color fadeMirror(const Color& start, const Color& end, uint32_t duration,
                         uint32_t currentStep) {
  if (currentStep >= duration) return end;
  if (currentStep == 0) return start;
  auto mix = [&](uint8_t s, uint8_t e) -> uint8_t {
    return static_cast<uint8_t>(
        static_cast<int32_t>(s) +
        (static_cast<int32_t>(e) - static_cast<int32_t>(s)) *
            static_cast<int64_t>(currentStep) / duration);
  };
  return Color(mix(start.r, end.r), mix(start.g, end.g), mix(start.b, end.b),
               mix(start.w, end.w));
}

// Mirror of ota_indicator's internal paintBase settle math. Production
// derives settleStart/settleStep from static per-session state (nowMs vs. a
// latched quiet-entry time); the test passes them explicitly so this stays
// pure and hermetic across test cases.
void paintBase(FrameBuffer* baseFb, const std::vector<Color>& settleStart,
               uint32_t settleStep) {
  if (!baseFb) return;
  const size_t pixelCount = baseFb->buffer.size();
  const size_t defaultCount = baseFb->defaultColors.size();
  if (pixelCount == 0 || defaultCount == 0) return;

  const uint32_t clampedStep =
      settleStep >= kBaseSettleDurationMs ? kBaseSettleDurationMs : settleStep;

  for (size_t i = 0; i < pixelCount; i++) {
    const Color& target = baseFb->defaultColors[i < defaultCount ? i : defaultCount - 1];
    const Color& start = i < settleStart.size() ? settleStart[i] : target;
    baseFb->buffer[i] = fadeMirror(start, target, kBaseSettleDurationMs, clampedStep);
  }
}

// Stateful mirror of production paintBase's entry-edge handling
// (ota_indicator.cpp): s_entryMs/s_settleStart re-init on quietEntered only,
// same shape as the real static locals but scoped to one instance so tests
// don't leak state into each other.
class PaintBaseSession {
 public:
  void paint(FrameBuffer* baseFb, uint32_t nowMs, bool quietEntered) {
    if (!baseFb) return;
    const size_t pixelCount = baseFb->buffer.size();
    const size_t defaultCount = baseFb->defaultColors.size();
    if (pixelCount == 0 || defaultCount == 0) return;

    if (quietEntered) {
      entryMs_ = nowMs;
      settleStart_ = baseFb->buffer;
    }

    const uint32_t elapsed = nowMs - entryMs_;
    const uint32_t settleStep =
        elapsed >= kBaseSettleDurationMs ? kBaseSettleDurationMs : elapsed;

    for (size_t i = 0; i < pixelCount; i++) {
      const Color& target = baseFb->defaultColors[i < defaultCount ? i : defaultCount - 1];
      const Color& start = i < settleStart_.size() ? settleStart_[i] : target;
      baseFb->buffer[i] = fadeMirror(start, target, kBaseSettleDurationMs, settleStep);
    }
  }

 private:
  uint32_t entryMs_ = 0;
  std::vector<Color> settleStart_;
};

// Mirror of ota_indicator::paintSilent: settle shade and base independently
// toward their own defaultColors, no dim, no band. Two sessions (not one) so
// the surfaces' settle state can't cross-contaminate, same as production's
// separate s_shadeSettle/s_baseSettle.
void paintSilent(FrameBuffer* shadeFb, FrameBuffer* baseFb,
                  PaintBaseSession& shadeSession, PaintBaseSession& baseSession,
                  uint32_t nowMs, bool quietEntered) {
  shadeSession.paint(shadeFb, nowMs, quietEntered);
  baseSession.paint(baseFb, nowMs, quietEntered);
}

Color expectedDim(const Color& localBase) {
  return scaleColor(localBase, kDimScale255);
}

// Helper: peer progress pixels = peerBase at full brightness.
Color expectedPeer(const Color& peerBase) {
  return peerBase;
}

}  // namespace test

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// no-session path: only the dim background, no peer overlay.
// ---------------------------------------------------------------------------

void test_no_session_paints_dim_background_only() {
  test::FrameBuffer fb;
  fb.buffer.resize(10);
  const test::Color localBase(255, 0, 0, 0);  // red
  test::paint(&fb, localBase, 0, false, test::Color(0, 255, 0, 0), 0, 0);
  const test::Color expected = test::expectedDim(localBase);
  for (size_t i = 0; i < fb.buffer.size(); i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == expected);
  }
}

void test_zero_total_paints_dim_background_only() {
  test::FrameBuffer fb;
  fb.buffer.resize(5);
  const test::Color localBase(100, 100, 100, 0);
  test::paint(&fb, localBase, 0, true, test::Color(0, 255, 0, 0), 0, 0);
  const test::Color expected = test::expectedDim(localBase);
  for (size_t i = 0; i < fb.buffer.size(); i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == expected);
  }
}

// ---------------------------------------------------------------------------
// progress math: first N pixels = peer at full brightness, rest = dim.
// ---------------------------------------------------------------------------

void test_half_progress_paints_first_half_peer_rest_dim() {
  test::FrameBuffer fb;
  fb.buffer.resize(10);
  const test::Color localBase(255, 0, 0, 0);
  const test::Color peerBase(0, 0, 255, 0);
  // 50% of 10 pixels = 5 pixels of peer; sub-pixel edge at pixel 5.
  test::paint(&fb, localBase, 0, true, peerBase, 50, 100);

  // peerSolid = peerBase = (0, 0, 255, 0)
  const test::Color ep = test::expectedPeer(peerBase);
  const test::Color ed = test::expectedDim(localBase);

  // scaledProgress = 10*50*256/100 = 1280; wholePixels = 1280>>8 = 5, frac = 0.
  for (size_t i = 0; i < 5; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == ep);
  }
  // pixel 5 is the edge with frac=0: stays dim.
  for (size_t i = 5; i < 10; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == ed);
  }
}

void test_full_progress_paints_all_peer() {
  test::FrameBuffer fb;
  fb.buffer.resize(8);
  const test::Color localBase(255, 255, 255, 0);
  const test::Color peerBase(255, 255, 0, 0);  // yellow peer
  test::paint(&fb, localBase, 0, true, peerBase, 100, 100);

  // peerSolid = peerBase = (255, 255, 0, 0)
  const test::Color ep = test::expectedPeer(peerBase);
  for (size_t i = 0; i < 8; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == ep);
  }
}

void test_zero_progress_paints_no_peer_pixels() {
  test::FrameBuffer fb;
  fb.buffer.resize(6);
  const test::Color localBase(255, 0, 0, 0);
  const test::Color peerBase(0, 255, 0, 0);
  test::paint(&fb, localBase, 0, true, peerBase, 0, 100);
  const test::Color ed = test::expectedDim(localBase);
  for (size_t i = 0; i < 6; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == ed);
  }
}

void test_done_greater_than_total_clamps_to_full() {
  test::FrameBuffer fb;
  fb.buffer.resize(4);
  const test::Color localBase(0, 0, 0, 0);
  const test::Color peerBase(255, 0, 0, 0);
  test::paint(&fb, localBase, 0, true, peerBase, 200, 100);
  // peerSolid = peerBase = (255, 0, 0, 0)
  const test::Color ep = test::expectedPeer(peerBase);
  for (size_t i = 0; i < 4; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == ep);
  }
}

// ---------------------------------------------------------------------------
// Base surface is left untouched: paint() on the shade FB leaves a separate
// base FB unchanged (simulating the compositor painting shade only).
// ---------------------------------------------------------------------------

void test_base_surface_untouched() {
  test::FrameBuffer shade;
  shade.buffer.resize(6, test::Color(0, 0, 0, 0));
  test::FrameBuffer base;
  // Pre-fill base with a sentinel color to prove paint() never touches it.
  const test::Color sentinel(42, 99, 7, 0);
  base.buffer.resize(6, sentinel);

  const test::Color localBase(255, 0, 0, 0);
  const test::Color peerBase(0, 0, 255, 0);
  // Simulate the compositor: paint shade only, do not call paint on base.
  test::paint(&shade, localBase, 0, true, peerBase, 50, 100);

  // Shade should have indicator pixels.
  TEST_ASSERT_FALSE(shade.buffer[0] == test::Color(0, 0, 0, 0));
  // Base is unchanged.
  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == sentinel);
  }
}

// ---------------------------------------------------------------------------
// edge: empty buffer + null fb are no-ops (no crash).
// ---------------------------------------------------------------------------

void test_empty_buffer_noop() {
  test::FrameBuffer fb;
  test::paint(&fb, test::Color(255, 0, 0, 0), 0, true,
              test::Color(0, 255, 0, 0), 50, 100);
  TEST_ASSERT_EQUAL_UINT32(0, fb.buffer.size());
}

void test_null_fb_noop() {
  test::paint(nullptr, test::Color(255, 0, 0, 0), 0, true,
              test::Color(0, 255, 0, 0), 50, 100);
  TEST_PASS();  // no crash
}

// ---------------------------------------------------------------------------
// Base surface: settle toward defaultColors.
// ---------------------------------------------------------------------------

void test_base_settle_at_zero_step_holds_frozen_frame() {
  test::FrameBuffer base;
  const test::Color frozen(200, 10, 10, 0);
  base.buffer.assign(4, test::Color());
  base.defaultColors.assign(4, test::Color(0, 100, 0, 0));
  std::vector<test::Color> settleStart(4, frozen);

  test::paintBase(&base, settleStart, /*settleStep=*/0);

  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == frozen);
  }
}

void test_base_settle_reaches_default_colors_after_duration() {
  test::FrameBuffer base;
  const test::Color frozen(200, 10, 10, 0);
  const test::Color defaultColor(0, 100, 0, 0);
  base.buffer.assign(4, test::Color());
  base.defaultColors.assign(4, defaultColor);
  std::vector<test::Color> settleStart(4, frozen);

  test::paintBase(&base, settleStart, /*settleStep=*/test::kBaseSettleDurationMs);

  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == defaultColor);
  }
}

void test_base_settle_partial_step_moves_toward_target() {
  test::FrameBuffer base;
  const test::Color frozen(200, 10, 10, 0);
  const test::Color defaultColor(0, 100, 0, 0);
  base.buffer.assign(2, test::Color());
  base.defaultColors.assign(2, defaultColor);
  std::vector<test::Color> settleStart(2, frozen);

  test::paintBase(&base, settleStart, /*settleStep=*/test::kBaseSettleDurationMs / 2);

  // Green ramps 10 -> 100: partway through should be strictly between, not
  // still frozen and not yet at the default.
  TEST_ASSERT_TRUE(base.buffer[0].g > frozen.g);
  TEST_ASSERT_TRUE(base.buffer[0].g < defaultColor.g);
  TEST_ASSERT_FALSE(base.buffer[0] == frozen);
  TEST_ASSERT_FALSE(base.buffer[0] == defaultColor);
}

// Once settled, the base holds defaultColors unchanged across subsequent
// frames (no pulse re-modulating the pixels).
void test_base_holds_static_after_settle() {
  test::PaintBaseSession session;
  test::FrameBuffer base;
  const test::Color frozen(200, 10, 10, 0);
  const test::Color defaultColor(0, 100, 0, 0);
  base.buffer.assign(2, frozen);
  base.defaultColors.assign(2, defaultColor);

  session.paint(&base, /*nowMs=*/0, /*quietEntered=*/true);
  session.paint(&base, /*nowMs=*/test::kBaseSettleDurationMs, /*quietEntered=*/false);
  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == defaultColor);
  }

  // Several more frames well past settle: pixels stay exactly defaultColor.
  session.paint(&base, /*nowMs=*/test::kBaseSettleDurationMs + 1000,
                /*quietEntered=*/false);
  session.paint(&base, /*nowMs=*/test::kBaseSettleDurationMs + 9000,
                /*quietEntered=*/false);
  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == defaultColor);
  }
}

// ---------------------------------------------------------------------------
// Base surface: quiet-entry edge drives the settle re-init, a mid-session
// call without a new edge must not reset it.
// ---------------------------------------------------------------------------

void test_quiet_entry_edge_reinits_settle_from_current_frame() {
  test::PaintBaseSession session;
  test::FrameBuffer base;
  const test::Color midAnimation(80, 200, 30, 0);
  const test::Color defaultColor(0, 100, 0, 0);
  base.buffer.assign(2, midAnimation);
  base.defaultColors.assign(2, defaultColor);

  // Quiet entry at t=1000: settle must start from the frame on screen at
  // that moment (midAnimation), not from whatever was buffered earlier.
  session.paint(&base, /*nowMs=*/1000, /*quietEntered=*/true);
  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == midAnimation);
  }
}

void test_quiet_entry_edge_re_latches_on_second_entry() {
  test::PaintBaseSession session;
  test::FrameBuffer base;
  const test::Color firstFrozen(200, 10, 10, 0);
  const test::Color defaultColor(0, 100, 0, 0);
  base.buffer.assign(2, firstFrozen);
  base.defaultColors.assign(2, defaultColor);

  session.paint(&base, /*nowMs=*/0, /*quietEntered=*/true);
  session.paint(&base, /*nowMs=*/test::kBaseSettleDurationMs, /*quietEntered=*/false);
  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == defaultColor);
  }

  // Second quiet entry: buffer currently holds defaultColor. Overwrite it
  // with a new mid-animation frame right before the entry edge fires, the
  // way the compositor's normal pipeline would between OTA sessions.
  const test::Color secondFrozen(5, 5, 250, 0);
  base.buffer.assign(2, secondFrozen);
  session.paint(&base, /*nowMs=*/10000, /*quietEntered=*/true);
  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == secondFrozen);
  }
}

void test_no_entry_edge_does_not_reset_ongoing_settle() {
  test::PaintBaseSession session;
  test::FrameBuffer base;
  const test::Color frozen(200, 10, 10, 0);
  const test::Color defaultColor(0, 100, 0, 0);
  base.buffer.assign(2, frozen);
  base.defaultColors.assign(2, defaultColor);

  // Entry at t=0 latches settleStart=frozen.
  session.paint(&base, /*nowMs=*/0, /*quietEntered=*/true);
  // Halfway through the settle, no new entry edge: green ramps toward the
  // target from the ORIGINAL frozen frame, same as an uninterrupted session.
  session.paint(&base, /*nowMs=*/test::kBaseSettleDurationMs / 2,
                /*quietEntered=*/false);
  TEST_ASSERT_TRUE(base.buffer[0].g > frozen.g);
  TEST_ASSERT_TRUE(base.buffer[0].g < defaultColor.g);

  // A blocking stall (e.g. flash erase) that skips many frames, still no
  // entry edge: resuming at t=duration must reach the target, not restart
  // from whatever the buffer held mid-stall.
  session.paint(&base, /*nowMs=*/test::kBaseSettleDurationMs,
                /*quietEntered=*/false);
  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == defaultColor);
  }
}

// ---------------------------------------------------------------------------
// Silent (FS) OTA: paintSilent settles shade AND base toward their own
// defaultColors, independently, with no dim background and no progress band
// (unlike paint(), which dims+bands the shade every call).
// ---------------------------------------------------------------------------

void test_silent_settles_shade_and_base_independently_no_dim_no_band() {
  test::PaintBaseSession shadeSession;
  test::PaintBaseSession baseSession;
  test::FrameBuffer shade;
  test::FrameBuffer base;
  const test::Color shadeFrozen(200, 0, 0, 0);
  const test::Color baseFrozen(0, 200, 0, 0);
  const test::Color shadeDefault(0, 0, 200, 0);
  const test::Color baseDefault(0, 0, 0, 200);
  shade.buffer.assign(4, shadeFrozen);
  shade.defaultColors.assign(4, shadeDefault);
  base.buffer.assign(4, baseFrozen);
  base.defaultColors.assign(4, baseDefault);

  test::paintSilent(&shade, &base, shadeSession, baseSession,
                    /*nowMs=*/0, /*quietEntered=*/true);
  test::paintSilent(&shade, &base, shadeSession, baseSession,
                    /*nowMs=*/test::kBaseSettleDurationMs, /*quietEntered=*/false);

  // Each surface settles to its OWN default, not the dim-scaled localBase
  // (that math never runs here) and not swapped with the other surface's.
  for (size_t i = 0; i < shade.buffer.size(); i++) {
    TEST_ASSERT_TRUE(shade.buffer[i] == shadeDefault);
  }
  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == baseDefault);
  }
}

void test_silent_entry_edge_latches_each_surface_from_its_own_frame() {
  test::PaintBaseSession shadeSession;
  test::PaintBaseSession baseSession;
  test::FrameBuffer shade;
  test::FrameBuffer base;
  const test::Color shadeFrozen(80, 200, 30, 0);
  const test::Color baseFrozen(30, 80, 200, 0);
  shade.buffer.assign(2, shadeFrozen);
  shade.defaultColors.assign(2, test::Color(0, 0, 0, 0));
  base.buffer.assign(2, baseFrozen);
  base.defaultColors.assign(2, test::Color(255, 255, 255, 0));

  // On the entry tick (settleStep==0) each surface must read back its own
  // pre-quiet frame untouched, not the other surface's frozen frame.
  test::paintSilent(&shade, &base, shadeSession, baseSession,
                    /*nowMs=*/1000, /*quietEntered=*/true);
  for (size_t i = 0; i < shade.buffer.size(); i++) {
    TEST_ASSERT_TRUE(shade.buffer[i] == shadeFrozen);
  }
  for (size_t i = 0; i < base.buffer.size(); i++) {
    TEST_ASSERT_TRUE(base.buffer[i] == baseFrozen);
  }
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_no_session_paints_dim_background_only);
  RUN_TEST(test_zero_total_paints_dim_background_only);

  RUN_TEST(test_half_progress_paints_first_half_peer_rest_dim);
  RUN_TEST(test_full_progress_paints_all_peer);
  RUN_TEST(test_zero_progress_paints_no_peer_pixels);
  RUN_TEST(test_done_greater_than_total_clamps_to_full);

  RUN_TEST(test_base_surface_untouched);

  RUN_TEST(test_empty_buffer_noop);
  RUN_TEST(test_null_fb_noop);

  RUN_TEST(test_base_settle_at_zero_step_holds_frozen_frame);
  RUN_TEST(test_base_settle_reaches_default_colors_after_duration);
  RUN_TEST(test_base_settle_partial_step_moves_toward_target);
  RUN_TEST(test_base_holds_static_after_settle);

  RUN_TEST(test_quiet_entry_edge_reinits_settle_from_current_frame);
  RUN_TEST(test_quiet_entry_edge_re_latches_on_second_entry);
  RUN_TEST(test_no_entry_edge_does_not_reset_ongoing_settle);

  RUN_TEST(test_silent_settles_shade_and_base_independently_no_dim_no_band);
  RUN_TEST(test_silent_entry_edge_latches_each_surface_from_its_own_frame);

  return UNITY_END();
}
