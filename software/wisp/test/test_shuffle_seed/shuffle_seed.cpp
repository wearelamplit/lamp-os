// Native tests for the shuffleSeed parameter on sampleTupleForMac.
//
// Self-contained: algorithm redeclared locally (same pattern as
// test_tuple_sampler) so the test pins the contract without pulling in
// CurrentPalette / ESP-IDF headers.
//
// Two invariants:
//   1. seed=0 reproduces the pre-shuffle result (backward compat).
//   2. A non-zero seed produces a different (base,shade) pair for at least
//      one representative MAC + palette (the shuffle actually does something).

#include <unity.h>

#include <cstdint>
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

uint32_t fnv1a(const uint8_t* bytes, size_t n) {
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < n; ++i) {
    h ^= static_cast<uint32_t>(bytes[i]);
    h *= 0x01000193u;
  }
  return h;
}

uint32_t hashMac(const uint8_t mac[6], uint32_t salt) {
  uint8_t buf[6];
  buf[0] = mac[0] ^ static_cast<uint8_t>(salt & 0xFF);
  buf[1] = mac[1] ^ static_cast<uint8_t>((salt >> 8) & 0xFF);
  buf[2] = mac[2] ^ static_cast<uint8_t>((salt >> 16) & 0xFF);
  buf[3] = mac[3] ^ static_cast<uint8_t>((salt >> 24) & 0xFF);
  buf[4] = mac[4] ^ static_cast<uint8_t>(salt & 0xFF);
  buf[5] = mac[5] ^ static_cast<uint8_t>((salt >> 16) & 0xFF);
  uint32_t h = fnv1a(buf, 6);
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

constexpr uint32_t kGolden   = 0x9E3779B9u;
constexpr uint32_t kSwapSalt = 0xCAFEBABEu;

ColorTuple sampleTuple(const std::vector<RGBW>& stops, const uint8_t mac[6],
                       uint32_t shuffleSeed = 0) {
  ColorTuple out;
  if (stops.empty()) return out;
  const uint32_t n = static_cast<uint32_t>(stops.size());
  uint32_t idxA = hashMac(mac, 0u          ^ shuffleSeed) % n;
  uint32_t idxB = hashMac(mac, kGolden     ^ shuffleSeed) % n;
  if (n >= 2 && idxA == idxB) idxB = (idxB + 1) % n;
  const bool swap = (hashMac(mac, kSwapSalt ^ shuffleSeed) & 1u) != 0u;
  const RGBW& base  = swap ? stops[idxB] : stops[idxA];
  const RGBW& shade = swap ? stops[idxA] : stops[idxB];
  out.r[0] = base.r;  out.g[0] = base.g;  out.b[0] = base.b;  out.w[0] = base.w;
  out.r[1] = shade.r; out.g[1] = shade.g; out.b[1] = shade.b; out.w[1] = shade.w;
  return out;
}

bool tupleEqual(const ColorTuple& a, const ColorTuple& b) {
  for (int i = 0; i < 2; ++i) {
    if (a.r[i] != b.r[i] || a.g[i] != b.g[i] ||
        a.b[i] != b.b[i] || a.w[i] != b.w[i]) return false;
  }
  return true;
}

}  // namespace

void test_seed_zero_equals_no_seed(void) {
  // seed=0 must be byte-identical to the default (backward compat).
  const std::vector<RGBW> stops{
      {255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}, {255, 255, 0, 0}};
  const uint8_t mac[6] = {0x42, 0x42, 0x42, 0x42, 0x42, 0x42};
  const ColorTuple def  = sampleTuple(stops, mac);
  const ColorTuple seed0 = sampleTuple(stops, mac, 0u);
  TEST_ASSERT_TRUE_MESSAGE(tupleEqual(def, seed0),
      "seed=0 must reproduce the no-seed result");
}

void test_different_seed_produces_different_tuple(void) {
  // Across a fleet-like MAC sweep on a 4-color palette, at least one MAC
  // must get a different (base,shade) pair under seed=1 vs seed=0.
  const std::vector<RGBW> stops{
      {255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}, {255, 255, 0, 0}};
  bool found_diff = false;
  for (uint32_t i = 0; i < 256 && !found_diff; ++i) {
    const uint8_t mac[6] = {
        static_cast<uint8_t>(i & 0xFF),
        static_cast<uint8_t>((i >> 8) & 0xFF),
        0xAA, 0xBB, 0xCC, 0xDD};
    const ColorTuple t0 = sampleTuple(stops, mac, 0u);
    const ColorTuple t1 = sampleTuple(stops, mac, 1u);
    if (!tupleEqual(t0, t1)) found_diff = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(found_diff,
      "seed=1 must yield a different tuple for at least one MAC in a fleet sweep");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_seed_zero_equals_no_seed);
  RUN_TEST(test_different_seed_produces_different_tuple);
  return UNITY_END();
}
