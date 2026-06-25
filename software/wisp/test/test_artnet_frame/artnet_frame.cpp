// Native tests for the wisp ArtNet frame builder.
//
// Pins the on-the-wire layout that
// software/lamp-os/src/components/network/artnet.cpp decodes. If production
// drifts, these tests still express the spec.

#include <unity.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct RGBW {
  uint8_t r = 0, g = 0, b = 0, w = 0;
};

struct ColorTuple {
  uint8_t r[2] = {0, 0};
  uint8_t g[2] = {0, 0};
  uint8_t b[2] = {0, 0};
  uint8_t w[2] = {0, 0};
};

constexpr size_t kArtnetFrameSize = 530;
constexpr size_t kDmxStart = 18;
constexpr size_t kBytesPerFixture = 10;
constexpr size_t kNumFixtures = 8;

// Tuple sampler stub: deterministic, returns fixture-index-derived colors.
ColorTuple sampleTupleForFixture(const std::vector<RGBW>& palette,
                                 uint8_t fixtureIndex) {
  ColorTuple t;
  if (palette.empty()) return t;
  const auto& base = palette[fixtureIndex % palette.size()];
  const auto& shade = palette[(fixtureIndex + 1) % palette.size()];
  // Convention (mirrors TupleSampler.h:32): [0] → base, [1] → shade.
  t.r[0] = base.r;  t.g[0] = base.g;  t.b[0] = base.b;  t.w[0] = base.w;
  t.r[1] = shade.r; t.g[1] = shade.g; t.b[1] = shade.b; t.w[1] = shade.w;
  return t;
}

// Mirror of artnet_frame.cpp build logic.
// Returns bytes written, or 0 on insufficient buffer.
size_t buildFrame(const std::vector<RGBW>& palette,
                  uint8_t seq,
                  uint8_t* out, size_t outLen) {
  if (outLen < kArtnetFrameSize) return 0;
  std::memset(out, 0, kArtnetFrameSize);
  // ART_NET_ID
  const char kId[8] = {'A', 'r', 't', '-', 'N', 'e', 't', '\0'};
  std::memcpy(out, kId, 8);
  // OpCode 0x5000 (ART_DMX), little-endian
  out[8] = 0x00;
  out[9] = 0x50;
  // ProtVer 0x000E, big-endian
  out[10] = 0x00;
  out[11] = 0x0E;
  // Sequence
  out[12] = seq;
  // Physical
  out[13] = 0x00;
  // Universe 1, little-endian
  out[14] = 0x01;
  out[15] = 0x00;
  // Length 0x0200 = 512, big-endian
  out[16] = 0x02;
  out[17] = 0x00;
  // Fixtures
  for (uint8_t i = 0; i < kNumFixtures; ++i) {
    ColorTuple t = sampleTupleForFixture(palette, i);
    uint8_t* f = out + kDmxStart + (i * kBytesPerFixture);
    f[0] = t.r[1]; f[1] = t.g[1]; f[2] = t.b[1]; f[3] = t.w[1];  // shade ← t[1]
    f[4] = t.r[0]; f[5] = t.g[0]; f[6] = t.b[0]; f[7] = t.w[0];  // base  ← t[0]
    f[8] = 0;  // mode = pass-through
    f[9] = 0;  // parameter
  }
  return kArtnetFrameSize;
}

}  // namespace

void test_frame_size_is_530() {
  std::vector<RGBW> palette = {{255, 0, 0, 0}, {0, 255, 0, 0}};
  uint8_t buf[600] = {0};
  size_t n = buildFrame(palette, 0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_size_t(530u, n);
}

void test_frame_rejects_small_buffer() {
  std::vector<RGBW> palette = {{255, 0, 0, 0}};
  uint8_t buf[529] = {0};
  size_t n = buildFrame(palette, 0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_size_t(0u, n);
}

void test_header_is_artnet_dmx_universe_1() {
  std::vector<RGBW> palette = {{1, 2, 3, 4}};
  uint8_t buf[530] = {0};
  buildFrame(palette, 0x42, buf, sizeof(buf));
  // ID
  TEST_ASSERT_EQUAL_MEMORY("Art-Net\0", buf, 8);
  // OpCode ART_DMX 0x5000 LE
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[8]);
  TEST_ASSERT_EQUAL_UINT8(0x50, buf[9]);
  // ProtVer 0x000E BE
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[10]);
  TEST_ASSERT_EQUAL_UINT8(0x0E, buf[11]);
  // Sequence
  TEST_ASSERT_EQUAL_UINT8(0x42, buf[12]);
  // Physical
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[13]);
  // Universe 1 LE
  TEST_ASSERT_EQUAL_UINT8(0x01, buf[14]);
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[15]);
  // Length 512 BE
  TEST_ASSERT_EQUAL_UINT8(0x02, buf[16]);
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[17]);
}

void test_first_fixture_carries_base_then_shade() {
  // Two-color palette; fixture 0's tuple is base=palette[0], shade=palette[1]
  // per the stub sampler. After the build, shade slot (0..3) gets shade =
  // palette[1] = {50,60,70,80}; base slot (4..7) gets base = palette[0] =
  // {10,20,30,40}.
  std::vector<RGBW> palette = {{10, 20, 30, 40}, {50, 60, 70, 80}};
  uint8_t buf[530] = {0};
  buildFrame(palette, 0, buf, sizeof(buf));
  // Shade slot at fixture 0 offset 18..21 = palette[1] = {50,60,70,80}.
  TEST_ASSERT_EQUAL_UINT8(50, buf[18]);
  TEST_ASSERT_EQUAL_UINT8(60, buf[19]);
  TEST_ASSERT_EQUAL_UINT8(70, buf[20]);
  TEST_ASSERT_EQUAL_UINT8(80, buf[21]);
  // Base slot at 22..25 = palette[0] = {10,20,30,40}.
  TEST_ASSERT_EQUAL_UINT8(10, buf[22]);
  TEST_ASSERT_EQUAL_UINT8(20, buf[23]);
  TEST_ASSERT_EQUAL_UINT8(30, buf[24]);
  TEST_ASSERT_EQUAL_UINT8(40, buf[25]);
  // Mode + parameter zeroed.
  TEST_ASSERT_EQUAL_UINT8(0, buf[26]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[27]);
}

void test_unused_fixtures_zeroed_with_empty_palette() {
  std::vector<RGBW> palette;
  uint8_t buf[530];
  std::memset(buf, 0xFF, sizeof(buf));
  buildFrame(palette, 0, buf, sizeof(buf));
  // All 80 fixture bytes zero.
  for (size_t i = 18; i < 18 + 80; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
  }
  for (size_t i = 18 + 80; i < 530; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
  }
}

void test_eight_fixtures_emitted() {
  std::vector<RGBW> palette;
  for (uint8_t i = 0; i < 8; ++i) palette.push_back({uint8_t(i * 16), 0, 0, 0});
  uint8_t buf[530] = {0};
  buildFrame(palette, 0, buf, sizeof(buf));
  for (uint8_t i = 0; i < 8; ++i) {
    size_t off = 18 + (i * 10);
    uint8_t expectedShadeR = ((i + 1) % 8) * 16;
    TEST_ASSERT_EQUAL_UINT8(expectedShadeR, buf[off + 0]);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_frame_size_is_530);
  RUN_TEST(test_frame_rejects_small_buffer);
  RUN_TEST(test_header_is_artnet_dmx_universe_1);
  RUN_TEST(test_first_fixture_carries_base_then_shade);
  RUN_TEST(test_unused_fixtures_zeroed_with_empty_palette);
  RUN_TEST(test_eight_fixtures_emitted);
  return UNITY_END();
}
