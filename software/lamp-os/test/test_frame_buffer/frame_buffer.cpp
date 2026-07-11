// Native tests for FrameBuffer multi-segment fanout.
//
// Adafruit_NeoPixel.h is stubbed by test/native_stubs/Adafruit_NeoPixel.h,
// found via -I test/native_stubs in the native env build_flags.

#include <unity.h>

#include <cstdint>
#include <vector>

#include "../../src/core/frame_buffer.cpp"

using namespace lamp;

static Adafruit_NeoPixel neo0;
static Adafruit_NeoPixel neo1;

void setUp() {
  neo0 = Adafruit_NeoPixel{};
  neo1 = Adafruit_NeoPixel{};
}
void tearDown() {}

static uint32_t encode(const Color& c) {
  return (uint32_t)((c.w << 24) | (c.r << 16) | (c.g << 8) | c.b);
}

// New API and old adapter produce identical setPixelColor call sequences.
void test_new_api_and_adapter_flush_identically() {
  const Color red(0xFF, 0, 0, 0);
  const Color blue(0, 0, 0xFF, 0);

  FrameBuffer fb1;
  fb1.begin({red, blue, red}, std::vector<StripSegment>{{&neo0, "test", 0, 3}});
  fb1.buffer = {red, blue, red};
  neo0.reset();
  fb1.flush();
  auto calls1 = neo0.pixelCalls;

  FrameBuffer fb2;
  fb2.begin({red, blue, red}, 3, &neo0);
  fb2.buffer = {red, blue, red};
  neo0.reset();
  fb2.flush();
  auto calls2 = neo0.pixelCalls;

  TEST_ASSERT_EQUAL_UINT(calls1.size(), calls2.size());
  for (size_t i = 0; i < calls1.size(); i++) {
    TEST_ASSERT_EQUAL_UINT16(calls1[i].n, calls2[i].n);
    TEST_ASSERT_EQUAL_UINT32(calls1[i].c, calls2[i].c);
  }
}

// 2-segment buffer routes buffer[0..a) to seg0, buffer[a..a+b) to seg1.
void test_multisegment_routes_pixels_to_correct_drivers() {
  const uint16_t a = 3, b = 4;
  const Color red(0xFF, 0, 0, 0);
  const Color green(0, 0xFF, 0, 0);

  FrameBuffer fb;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "s0", 0, a}, {&neo1, "s1", a, b}});

  TEST_ASSERT_EQUAL_UINT8(a + b, fb.pixelCount);

  for (uint16_t i = 0; i < a; i++) fb.buffer[i] = red;
  for (uint16_t i = 0; i < b; i++) fb.buffer[a + i] = green;

  fb.flush();

  TEST_ASSERT_EQUAL_UINT(a, neo0.pixelCalls.size());
  for (size_t i = 0; i < neo0.pixelCalls.size(); i++) {
    TEST_ASSERT_EQUAL_UINT16(i, neo0.pixelCalls[i].n);
    TEST_ASSERT_EQUAL_UINT32(encode(red), neo0.pixelCalls[i].c);
  }

  TEST_ASSERT_EQUAL_UINT(b, neo1.pixelCalls.size());
  for (size_t i = 0; i < neo1.pixelCalls.size(); i++) {
    TEST_ASSERT_EQUAL_UINT16(i, neo1.pixelCalls[i].n);
    TEST_ASSERT_EQUAL_UINT32(encode(green), neo1.pixelCalls[i].c);
  }
}

// canShow() false on one segment: show() skipped and previousBuffer not advanced,
// so the next flush retransmits rather than short-circuiting.
void test_skipped_segment_leaves_previous_buffer_unadvanced() {
  const Color red(0xFF, 0, 0, 0);
  const Color blue(0, 0, 0xFF, 0);

  FrameBuffer fb;
  neo0.canShowResult = true;
  neo1.canShowResult = true;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "s0", 0, 2}, {&neo1, "s1", 2, 2}});

  // Establish a fully-shown baseline.
  fb.buffer = {red, red, red, red};
  fb.flush();
  TEST_ASSERT_TRUE(fb.previousBuffer == fb.buffer);

  // Block neo1 and change the buffer.
  neo1.canShowResult = false;
  fb.buffer = {blue, blue, blue, blue};
  neo0.reset();
  neo1.reset();
  fb.flush();

  TEST_ASSERT_EQUAL_INT(1, neo0.showCount);
  TEST_ASSERT_EQUAL_INT(0, neo1.showCount);
  // previousBuffer stays at the last fully-shown state, not the current buffer.
  TEST_ASSERT_FALSE(fb.previousBuffer == fb.buffer);

  // Next flush with the same buffer: not short-circuited (previousBuffer still old).
  neo0.reset();
  neo1.reset();
  fb.flush();
  TEST_ASSERT_EQUAL_UINT(2, neo0.pixelCalls.size());
  TEST_ASSERT_EQUAL_UINT(2, neo1.pixelCalls.size());
  TEST_ASSERT_EQUAL_INT(0, neo1.showCount);

  // Unblock neo1: full flush advances previousBuffer.
  neo1.canShowResult = true;
  neo0.reset();
  neo1.reset();
  fb.flush();
  TEST_ASSERT_EQUAL_INT(1, neo1.showCount);
  TEST_ASSERT_TRUE(fb.previousBuffer == fb.buffer);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_new_api_and_adapter_flush_identically);
  RUN_TEST(test_multisegment_routes_pixels_to_correct_drivers);
  RUN_TEST(test_skipped_segment_leaves_previous_buffer_unadvanced);
  return UNITY_END();
}
