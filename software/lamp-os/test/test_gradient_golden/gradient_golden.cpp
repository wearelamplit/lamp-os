// Pins buildGradientWithStops byte for byte. Each hash is FNV-1a over the real
// builder's output. A refactor that changes any
// byte of any open gradient fails here.

#include <unity.h>

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"

using namespace lamp;

void setUp() {}
void tearDown() {}

static constexpr std::array<uint8_t, 9> kPixelCounts = {1, 2, 7, 12, 35, 36, 38, 40, 255};
static constexpr std::array<size_t, 11> kStopCounts = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 23};

// Every channel varies from stop to stop, with ascending and descending pairs
// on each channel inside the first eight stops; a descending pair takes the
// unsigned-wrap path of easeLinear.
static std::vector<Color> stopFixture(size_t n) {
  static constexpr std::array<Color, 23> kStops = {{
      {0xFF, 0x00, 0x10, 0x80}, {0x00, 0xFF, 0x90, 0x05}, {0x7A, 0x13, 0xFF, 0xC4},
      {0x21, 0xE8, 0x02, 0x3B}, {0xD9, 0x44, 0x6E, 0xFF}, {0x03, 0x9B, 0xD1, 0x00},
      {0xA6, 0x01, 0x37, 0x92}, {0x58, 0xC7, 0xF0, 0x1D}, {0xEE, 0x2A, 0x05, 0x66},
      {0x11, 0x77, 0xB3, 0xDA}, {0x9C, 0xFE, 0x48, 0x0F}, {0x40, 0x09, 0x8D, 0xA1},
      {0xC3, 0x5E, 0x1A, 0x7C}, {0x06, 0xD4, 0xE9, 0x30}, {0x8F, 0x31, 0x62, 0xF5},
      {0xF2, 0x88, 0x0B, 0x47}, {0x2D, 0x10, 0xC8, 0xB9}, {0xB7, 0xA3, 0x56, 0x02},
      {0x64, 0x3C, 0xFA, 0x8E}, {0x19, 0xEB, 0x27, 0x53}, {0xDE, 0x05, 0x99, 0xC0},
      {0x4B, 0xB6, 0x70, 0x18}, {0x95, 0x22, 0xDC, 0xE7},
  }};
  return std::vector<Color>(kStops.begin(), kStops.begin() + n);
}

static uint64_t fnv1a(uint64_t h, uint8_t byte) {
  return (h ^ byte) * 0x100000001b3ULL;
}

static uint64_t hashGradient(uint64_t h, const std::vector<Color>& px) {
  for (uint8_t b : {static_cast<uint8_t>(px.size()), static_cast<uint8_t>(px.size() >> 8)}) {
    h = fnv1a(h, b);
  }
  for (const Color& c : px) {
    h = fnv1a(fnv1a(fnv1a(fnv1a(h, c.r), c.g), c.b), c.w);
  }
  return h;
}

static constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ULL;

static uint64_t hashStopCount(size_t stops) {
  uint64_t h = kFnvBasis;
  for (uint8_t px : kPixelCounts) {
    h = hashGradient(h, buildGradientWithStops(px, stopFixture(stops)));
  }
  return h;
}

static void expectHash(uint64_t expected, uint64_t actual, const char* label) {
  char msg[96];
  std::snprintf(msg, sizeof msg, "%s: actual 0x%016" PRIx64, label, actual);
  TEST_ASSERT_EQUAL_HEX64_MESSAGE(expected, actual, msg);
}

void test_fixture_has_both_channel_directions() {
  const auto stops = stopFixture(8);
  for (int ch = 0; ch < 4; ++ch) {
    bool up = false, down = false;
    for (size_t k = 0; k + 1 < stops.size(); ++k) {
      const uint8_t a = (&stops[k].r)[ch];
      const uint8_t b = (&stops[k + 1].r)[ch];
      up |= b > a;
      down |= b < a;
    }
    TEST_ASSERT_TRUE(up);
    TEST_ASSERT_TRUE(down);
  }
}

static constexpr std::array<uint64_t, kStopCounts.size()> kGolden = {
    0x2067d152bfdf36b3ULL, 0x97619006af7ce09bULL, 0x23164f51c423cd85ULL,
    0x3b11dd8fbe7f9c01ULL, 0x608787e678e8f861ULL, 0x049e7efe8385ce90ULL,
    0xfed3132dc5a4e3baULL, 0xbf924712851a7e8cULL, 0xbf924712851a7e8cULL,
    0xbf924712851a7e8cULL, 0xbf924712851a7e8cULL,
};

void test_golden_per_stop_count() {
  for (size_t i = 0; i < kStopCounts.size(); ++i) {
    char label[32];
    std::snprintf(label, sizeof label, "%zu stops", kStopCounts[i]);
    expectHash(kGolden[i], hashStopCount(kStopCounts[i]), label);
  }
}

// Every pixel count 0 to 255 for 0 to 23 stops.
static constexpr uint64_t kGoldenAll = 0xa0de486b0244034aULL;

void test_golden_every_pixel_count() {
  uint64_t h = kFnvBasis;
  for (size_t stops = 0; stops <= 23; ++stops) {
    for (int px = 0; px <= 255; ++px) {
      h = hashGradient(h, buildGradientWithStops(static_cast<uint8_t>(px), stopFixture(stops)));
    }
  }
  expectHash(kGoldenAll, h, "all");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fixture_has_both_channel_directions);
  RUN_TEST(test_golden_per_stop_count);
  RUN_TEST(test_golden_every_pixel_count);
  return UNITY_END();
}
