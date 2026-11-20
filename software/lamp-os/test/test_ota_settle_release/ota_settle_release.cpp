// The real OTA indicator holds the frames a settle starts from only while the
// settle runs. It frees them once the settle completes, or when quiet mode
// ends first.

#include <unity.h>

#include "../../src/components/firmware/ota_indicator.cpp"
#include "../../src/components/network/mesh/lamp_roster.cpp"
#include "../../src/core/peer_view.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../replaced_new/heap_tally.hpp"

lamp::FirmwareReceiver firmwareReceiver;

namespace lamp {
FirmwareDistributor firmwareDistributor;
bool FirmwareDistributor::isInProgress() const { return false; }
bool FirmwareDistributor::getLastSession(uint8_t*, uint16_t&) const { return false; }
}  // namespace lamp

using lamp::Color;
using lamp::FrameBuffer;

static constexpr uint32_t kEntryMs = 5000;
static constexpr uint32_t kMidSettleMs = kEntryMs + 100;
static constexpr uint32_t kSettledMs = kEntryMs + 600;

static constexpr int kPixels = 12;
// Shade and base each capture one start frame on quiet entry.
static constexpr size_t kStartFramesBytes = 2 * kPixels * sizeof(Color);

static void fill(FrameBuffer& fb) {
  fb.pixelCount = kPixels;
  fb.buffer.assign(kPixels, Color(0x40, 0x10, 0x80, 0));
  fb.defaultColors.assign(kPixels, Color(0, 0, 0xFF, 0));
}

void setUp() {}
void tearDown() {}

static void paintAt(bool visible, FrameBuffer& shade, FrameBuffer& base, uint32_t nowMs,
                    bool entered) {
  if (visible) {
    lamp::ota_indicator::paint(&shade, Color(0, 0, 0xFF, 0), nowMs, &base, entered);
  } else {
    lamp::ota_indicator::paintSilent(&shade, &base, nowMs, entered);
  }
}

// Quiet mode starts, paints once more at lastPaintMs, then ends.
static size_t heldAfterQuiet(bool visible, uint32_t lastPaintMs) {
  FrameBuffer shade;
  FrameBuffer base;
  fill(shade);
  fill(base);
  const size_t before = heap_tally::g_liveBytes;
  paintAt(visible, shade, base, kEntryMs, true);
  TEST_ASSERT_EQUAL_size_t(kStartFramesBytes, heap_tally::g_liveBytes - before);
  paintAt(visible, shade, base, lastPaintMs, false);
  if (lastPaintMs < kSettledMs) lamp::ota_indicator::quietExited();
  return heap_tally::g_liveBytes - before;
}

void test_completed_visible_settle_frees_its_frames() {
  TEST_ASSERT_EQUAL_size_t(0, heldAfterQuiet(true, kSettledMs));
}

void test_completed_silent_settle_frees_its_frames() {
  TEST_ASSERT_EQUAL_size_t(0, heldAfterQuiet(false, kSettledMs));
}

void test_short_visible_quiet_frees_its_settle_frames() {
  TEST_ASSERT_EQUAL_size_t(0, heldAfterQuiet(true, kMidSettleMs));
}

void test_short_silent_quiet_frees_its_settle_frames() {
  TEST_ASSERT_EQUAL_size_t(0, heldAfterQuiet(false, kMidSettleMs));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_completed_visible_settle_frees_its_frames);
  RUN_TEST(test_completed_silent_settle_frees_its_frames);
  RUN_TEST(test_short_visible_quiet_frees_its_settle_frames);
  RUN_TEST(test_short_silent_quiet_frees_its_settle_frames);
  return UNITY_END();
}
