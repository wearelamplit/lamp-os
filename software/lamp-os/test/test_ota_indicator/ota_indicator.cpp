// Native-host unit tests for the OTA visual indicator paint.
//
// Mirror the production paint() shape inline (per the codebase convention
// for tests in test/test_color, test/test_firmware_receiver, etc.). The
// native env doesn't build src/ because the production indicator pulls in
// the firmware receiver/distributor headers which need ESP IDF; the
// observable contract is the per-pixel output for a given (localBase,
// peerBase, progress, pulse) tuple, so we pin that here.
//
// Production code is at src/core/ota_indicator.{hpp,cpp}. Both implement
// the same math: 20% dim background of localBase, peerBase scaled by a
// 0..255 pulse value on the first (pixelCount * done / total) pixels.

#include <unity.h>

#include <math.h>

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
};

constexpr uint8_t kDimScale255 = 51;  // 20%

inline Color scaleColor(const Color& c, uint8_t num) {
  return Color(
      static_cast<uint8_t>((static_cast<uint16_t>(c.r) * num) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.g) * num) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.b) * num) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.w) * num) / 255));
}

uint8_t pulseScale255(uint32_t nowMs) {
  constexpr float kPeriodMs = 1500.0f;
  constexpr float kTwoPi = 6.28318530718f;
  const float phase = (kTwoPi * static_cast<float>(nowMs % 1500u)) / kPeriodMs;
  const float s = (sinf(phase) + 1.0f) * 0.5f;
  const int v = 51 + static_cast<int>(204.0f * s);
  if (v < 51) return 51;
  if (v > 255) return 255;
  return static_cast<uint8_t>(v);
}

// Mirror of ota_indicator::paint with the OTA session state passed
// explicitly (no firmwareReceiver / firmwareDistributor globals reachable
// from the native env).
void paint(FrameBuffer* fb, const Color& localBase, uint32_t nowMs,
           bool haveSession, const Color& peerBase,
           uint32_t done, uint32_t total) {
  if (!fb) return;
  const size_t pixelCount = fb->buffer.size();
  if (pixelCount == 0) return;

  const Color dim = scaleColor(localBase, kDimScale255);
  for (size_t i = 0; i < pixelCount; i++) fb->buffer[i] = dim;
  if (!haveSession || total == 0) return;

  const uint8_t pulse = pulseScale255(nowMs);
  const Color pulsedPeer = scaleColor(peerBase, pulse);
  if (done > total) done = total;
  const size_t progressPixels =
      static_cast<size_t>((static_cast<uint64_t>(pixelCount) * done) / total);
  for (size_t i = 0; i < progressPixels && i < pixelCount; i++) {
    fb->buffer[i] = pulsedPeer;
  }
}

}  // namespace test

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// pulse math: 51 + 204 * (sin(2π * t / 1500) + 1) / 2.
//   t=0     → sin=0  → pulse = 51 + 102 = 153 (midpoint).
//   t=375   → sin=1  → pulse = 51 + 204 = 255 (peak).
//   t=750   → sin=0  → pulse = 153 again (midpoint, descending).
//   t=1125  → sin=-1 → pulse = 51 (floor).
// The per-pixel tests below use t=1125 to pin the deterministic "peerBase
// at 20%" floor.
// ---------------------------------------------------------------------------

void test_pulse_at_zero_returns_midpoint() {
  // sin(0)=0 → 51 + 204 * 0.5 = 153.
  const uint8_t v = test::pulseScale255(0);
  TEST_ASSERT_TRUE(v >= 152 && v <= 154);
}

void test_pulse_at_quarter_period_peaks() {
  // 375 ms = period/4 → sin(pi/2) = 1 → pulse = 51 + 204 = 255.
  const uint8_t v = test::pulseScale255(375);
  TEST_ASSERT_TRUE(v >= 250);
}

void test_pulse_at_three_quarter_period_floors() {
  // 1125 ms = 3*period/4 → sin(3pi/2) = -1 → pulse = 51 (the 20% floor).
  const uint8_t v = test::pulseScale255(1125);
  TEST_ASSERT_EQUAL_UINT8(51, v);
}

// ---------------------------------------------------------------------------
// no-session path: only the dim background, no peer overlay.
// ---------------------------------------------------------------------------

void test_no_session_paints_dim_background_only() {
  test::FrameBuffer fb;
  fb.buffer.resize(10);
  const test::Color localBase(255, 0, 0, 0);  // red
  test::paint(&fb, localBase, /*nowMs=*/0, /*haveSession=*/false,
              test::Color(0, 255, 0, 0), 0, 0);
  // Every pixel should be red at 20%: (255*51/255, 0, 0, 0) = (51,0,0,0)
  const test::Color expected(51, 0, 0, 0);
  for (size_t i = 0; i < fb.buffer.size(); i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == expected);
  }
}

void test_zero_total_paints_dim_background_only() {
  test::FrameBuffer fb;
  fb.buffer.resize(5);
  const test::Color localBase(100, 100, 100, 0);
  test::paint(&fb, localBase, 0, /*haveSession=*/true,
              test::Color(0, 255, 0, 0), 0, 0);
  // localBase * 51/255 = 20 per channel.
  const test::Color expected(20, 20, 20, 0);
  for (size_t i = 0; i < fb.buffer.size(); i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == expected);
  }
}

// ---------------------------------------------------------------------------
// progress math: first N pixels = peer color, rest = local dim.
// nowMs=0 so the pulse floor is 51 → peer pixels are peer * 20%.
// ---------------------------------------------------------------------------

void test_half_progress_paints_first_half_peer_rest_dim() {
  test::FrameBuffer fb;
  fb.buffer.resize(10);
  const test::Color localBase(255, 0, 0, 0);   // red lamp
  const test::Color peerBase(0, 0, 255, 0);    // peer is blue
  // 50% of 10 pixels = 5 pixels of peer color.
  // nowMs=1125 → pulse at 20% floor (51/255) for deterministic peer val.
  test::paint(&fb, localBase, /*nowMs=*/1125, /*haveSession=*/true, peerBase,
              /*done=*/50, /*total=*/100);

  // peer at 20% pulse floor: (0,0,51,0)
  const test::Color expectedPeer(0, 0, 51, 0);
  // local at 20% dim: (51,0,0,0)
  const test::Color expectedLocal(51, 0, 0, 0);

  for (size_t i = 0; i < 5; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == expectedPeer);
  }
  for (size_t i = 5; i < 10; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == expectedLocal);
  }
}

void test_full_progress_paints_all_peer() {
  test::FrameBuffer fb;
  fb.buffer.resize(8);
  const test::Color localBase(255, 255, 255, 0);
  const test::Color peerBase(255, 255, 0, 0);  // peer yellow
  test::paint(&fb, localBase, /*nowMs=*/1125, true, peerBase,
              /*done=*/100, /*total=*/100);

  // peer * 51/255 = (51, 51, 0, 0)
  const test::Color expectedPeer(51, 51, 0, 0);
  for (size_t i = 0; i < 8; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == expectedPeer);
  }
}

void test_zero_progress_paints_no_peer_pixels() {
  test::FrameBuffer fb;
  fb.buffer.resize(6);
  const test::Color localBase(255, 0, 0, 0);
  const test::Color peerBase(0, 255, 0, 0);
  test::paint(&fb, localBase, 1125, true, peerBase, /*done=*/0, /*total=*/100);
  const test::Color expectedDim(51, 0, 0, 0);
  for (size_t i = 0; i < 6; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == expectedDim);
  }
}

void test_done_greater_than_total_clamps_to_full() {
  // Defensive: sender briefly racing ahead of total chunks shouldn't
  // overrun the buffer. paint() clamps done at total.
  test::FrameBuffer fb;
  fb.buffer.resize(4);
  const test::Color localBase(0, 0, 0, 0);
  const test::Color peerBase(255, 0, 0, 0);
  test::paint(&fb, localBase, 1125, true, peerBase, /*done=*/200, /*total=*/100);
  const test::Color expectedPeer(51, 0, 0, 0);
  for (size_t i = 0; i < 4; i++) {
    TEST_ASSERT_TRUE(fb.buffer[i] == expectedPeer);
  }
}

// ---------------------------------------------------------------------------
// edge: empty buffer + null fb are no-ops (no crash).
// ---------------------------------------------------------------------------

void test_empty_buffer_noop() {
  test::FrameBuffer fb;
  // buffer.size() == 0 — paint just returns.
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
// pulse at the peak should give peer at full brightness (within float slop).
// ---------------------------------------------------------------------------

void test_full_progress_at_pulse_peak_is_full_peer_color() {
  test::FrameBuffer fb;
  fb.buffer.resize(3);
  const test::Color localBase(0, 0, 0, 0);
  const test::Color peerBase(255, 128, 64, 0);
  test::paint(&fb, localBase, /*nowMs=*/375, true, peerBase, 100, 100);
  // pulse=255 → peer * 255/255 = peer itself.
  for (size_t i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_UINT8(255, fb.buffer[i].r);
    TEST_ASSERT_EQUAL_UINT8(128, fb.buffer[i].g);
    TEST_ASSERT_EQUAL_UINT8(64, fb.buffer[i].b);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].w);
  }
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_pulse_at_zero_returns_midpoint);
  RUN_TEST(test_pulse_at_quarter_period_peaks);
  RUN_TEST(test_pulse_at_three_quarter_period_floors);

  RUN_TEST(test_no_session_paints_dim_background_only);
  RUN_TEST(test_zero_total_paints_dim_background_only);

  RUN_TEST(test_half_progress_paints_first_half_peer_rest_dim);
  RUN_TEST(test_full_progress_paints_all_peer);
  RUN_TEST(test_zero_progress_paints_no_peer_pixels);
  RUN_TEST(test_done_greater_than_total_clamps_to_full);

  RUN_TEST(test_empty_buffer_noop);
  RUN_TEST(test_null_fb_noop);

  RUN_TEST(test_full_progress_at_pulse_peak_is_full_peer_color);

  return UNITY_END();
}
