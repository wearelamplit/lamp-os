// Native tests for TupleSampler (wisp paint distribution).
//
// Self-contained: the algorithm is redeclared here as a local helper rather
// than linking the production .cpp. This keeps the test portable (no
// CurrentPalette / aurora pull-in) and pins the algorithm — if production
// drifts the test still expresses the spec.

#include <unity.h>

#include <algorithm>
#include <cmath>
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

// --- Mirror of TupleSampler.cpp internals ---

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

bool nearlyEqualColor(const RGBW& a, const RGBW& b) {
  constexpr int kDeltaThreshold = 8;
  auto d = [](uint8_t x, uint8_t y) {
    return std::abs(static_cast<int>(x) - static_cast<int>(y));
  };
  return d(a.r, b.r) < kDeltaThreshold && d(a.g, b.g) < kDeltaThreshold &&
         d(a.b, b.b) < kDeltaThreshold && d(a.w, b.w) < kDeltaThreshold;
}

std::vector<RGBW> dedupe(const std::vector<RGBW>& in) {
  std::vector<RGBW> out;
  out.reserve(in.size());
  for (const auto& c : in) {
    if (out.empty() || !nearlyEqualColor(out.back(), c)) {
      out.push_back(c);
    }
  }
  return out;
}

constexpr uint32_t kGolden   = 0x9E3779B9u;
constexpr uint32_t kSwapSalt = 0xCAFEBABEu;

ColorTuple sampleTuple(const std::vector<RGBW>& raw, const uint8_t mac[6]) {
  ColorTuple out;
  if (raw.empty()) return out;
  auto stops = dedupe(raw);
  if (stops.empty()) return out;

  const uint32_t n = static_cast<uint32_t>(stops.size());
  uint32_t idxA = hashMac(mac, 0u)      % n;
  uint32_t idxB = hashMac(mac, kGolden) % n;
  if (n >= 2 && idxA == idxB) idxB = (idxB + 1) % n;

  const bool swap = (hashMac(mac, kSwapSalt) & 1u) != 0u;
  const RGBW& base  = swap ? stops[idxB] : stops[idxA];
  const RGBW& shade = swap ? stops[idxA] : stops[idxB];
  out.r[0] = base.r;  out.g[0] = base.g;  out.b[0] = base.b;  out.w[0] = base.w;
  out.r[1] = shade.r; out.g[1] = shade.g; out.b[1] = shade.b; out.w[1] = shade.w;
  return out;
}

// Returns true if `c` is byte-exactly one of the authored stops.
bool isOneOfAuthoredStops(const std::vector<RGBW>& stops, uint8_t r, uint8_t g,
                          uint8_t b, uint8_t w) {
  for (const auto& s : stops) {
    if (s.r == r && s.g == g && s.b == b && s.w == w) return true;
  }
  return false;
}

}  // namespace

// --- Tests ---

void test_empty_palette_returns_black(void) {
  std::vector<RGBW> raw;
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03};
  ColorTuple t = sampleTuple(raw, mac);
  for (int i = 0; i < 2; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0, t.r[i]);
    TEST_ASSERT_EQUAL_UINT8(0, t.g[i]);
    TEST_ASSERT_EQUAL_UINT8(0, t.b[i]);
    TEST_ASSERT_EQUAL_UINT8(0, t.w[i]);
  }
}

void test_single_color_palette_returns_color_for_both(void) {
  std::vector<RGBW> raw{{200, 100, 50, 0}};
  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
  ColorTuple t = sampleTuple(raw, mac);
  TEST_ASSERT_EQUAL_UINT8(200, t.r[0]);
  TEST_ASSERT_EQUAL_UINT8(100, t.g[0]);
  TEST_ASSERT_EQUAL_UINT8(50, t.b[0]);
  TEST_ASSERT_EQUAL_UINT8(200, t.r[1]);
  TEST_ASSERT_EQUAL_UINT8(100, t.g[1]);
  TEST_ASSERT_EQUAL_UINT8(50, t.b[1]);
}

void test_two_color_palette_picks_pure_endpoints_no_blending(void) {
  // The headline behavioural change: a 2-color palette must produce
  // tuples where BOTH colors are byte-exact authored stops — no
  // interpolation, no mid-tone blends. Each lamp gets {red, red},
  // {red, blue}, {blue, red}, or {blue, blue}.
  std::vector<RGBW> raw{{255, 0, 0, 0}, {0, 0, 255, 0}};
  for (uint32_t i = 0; i < 200; ++i) {
    uint8_t mac[6] = {static_cast<uint8_t>(i & 0xFF),
                      static_cast<uint8_t>((i >> 8) & 0xFF),
                      0x11, 0x22, 0x33, 0x44};
    ColorTuple t = sampleTuple(raw, mac);
    TEST_ASSERT_TRUE_MESSAGE(
        isOneOfAuthoredStops(raw, t.r[0], t.g[0], t.b[0], t.w[0]),
        "base surface must be a byte-exact authored stop");
    TEST_ASSERT_TRUE_MESSAGE(
        isOneOfAuthoredStops(raw, t.r[1], t.g[1], t.b[1], t.w[1]),
        "shade surface must be a byte-exact authored stop");
  }
}

void test_distinct_pair_when_palette_has_two_or_more_colors(void) {
  // For a multi-color palette, every lamp should get two DIFFERENT
  // authored colors (the idxA==idxB collision bump guarantees it).
  std::vector<RGBW> raw{{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0},
                        {255, 255, 0, 0}, {0, 255, 255, 0}};
  for (uint32_t i = 0; i < 200; ++i) {
    uint8_t mac[6] = {static_cast<uint8_t>(i & 0xFF),
                      static_cast<uint8_t>((i >> 8) & 0xFF),
                      0xAA, 0xBB, 0xCC, 0xDD};
    ColorTuple t = sampleTuple(raw, mac);
    const bool same = t.r[0] == t.r[1] && t.g[0] == t.g[1] &&
                      t.b[0] == t.b[1] && t.w[0] == t.w[1];
    TEST_ASSERT_FALSE_MESSAGE(
        same, "with >=2 distinct stops, base and shade must differ");
  }
}

void test_same_mac_returns_same_tuple(void) {
  std::vector<RGBW> raw{
      {255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}, {255, 255, 255, 0}};
  const uint8_t mac[6] = {0x42, 0x42, 0x42, 0x42, 0x42, 0x42};
  ColorTuple a = sampleTuple(raw, mac);
  ColorTuple b = sampleTuple(raw, mac);
  for (int i = 0; i < 2; ++i) {
    TEST_ASSERT_EQUAL_UINT8(a.r[i], b.r[i]);
    TEST_ASSERT_EQUAL_UINT8(a.g[i], b.g[i]);
    TEST_ASSERT_EQUAL_UINT8(a.b[i], b.b[i]);
    TEST_ASSERT_EQUAL_UINT8(a.w[i], b.w[i]);
  }
}

void test_swap_distribution_breaks_fixed_order(void) {
  // The other headline change: roughly half of all MACs should get the
  // swap bit set, meaning idxA goes to shade (not base). Without this
  // the fleet visually clamps to e.g. "all base = first-hash color".
  // Test: across a large MAC sweep on a 2-color palette, the count of
  // (base = red) lamps and (base = blue) lamps should each be within
  // 20% of half — i.e. neither dominates.
  std::vector<RGBW> raw{{255, 0, 0, 0}, {0, 0, 255, 0}};
  int baseRed = 0, baseBlue = 0;
  constexpr uint32_t kSweep = 1024;
  for (uint32_t i = 0; i < kSweep; ++i) {
    uint8_t mac[6] = {static_cast<uint8_t>(i & 0xFF),
                      static_cast<uint8_t>((i >> 8) & 0xFF),
                      static_cast<uint8_t>((i >> 16) & 0xFF),
                      0x99, 0x77, 0x55};
    ColorTuple t = sampleTuple(raw, mac);
    if (t.r[0] == 255 && t.b[0] == 0) baseRed++;
    if (t.r[0] == 0 && t.b[0] == 255) baseBlue++;
  }
  const int total = baseRed + baseBlue;
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(kSweep), total,
      "every MAC should resolve to one authored stop on each surface");
  const int low = (kSweep * 30) / 100;   // 30% floor
  const int hi  = (kSweep * 70) / 100;   // 70% ceiling
  TEST_ASSERT_TRUE_MESSAGE(baseRed >= low && baseRed <= hi,
      "base=red share should be within 30-70% (swap bit breaks the bias)");
  TEST_ASSERT_TRUE_MESSAGE(baseBlue >= low && baseBlue <= hi,
      "base=blue share should be within 30-70%");
}

void test_dedupe_drops_adjacent_near_identical_stops(void) {
  // Two near-identical stops followed by a distinct one. Dedupe collapses
  // them; no padding/interpolation happens, so the effective palette size
  // is exactly 2 distinct colors.
  std::vector<RGBW> raw{
      {100, 100, 100, 0},
      {103, 102, 101, 0},  // within 8/255 of previous → dropped
      {0, 0, 0, 0}};
  std::vector<RGBW> deduped = dedupe(raw);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(deduped.size()));
  TEST_ASSERT_EQUAL_UINT8(100, deduped.front().r);
  TEST_ASSERT_EQUAL_UINT8(100, deduped.front().g);
  TEST_ASSERT_EQUAL_UINT8(100, deduped.front().b);
  TEST_ASSERT_EQUAL_UINT8(0, deduped.back().r);
  TEST_ASSERT_EQUAL_UINT8(0, deduped.back().g);
  TEST_ASSERT_EQUAL_UINT8(0, deduped.back().b);
}

void test_picked_index_distance_varies_across_fleet(void) {
  // User complaint pre-fix: "always the same distance between colors
  // picked". With independent FNV hashes on idxA and idxB, the |idxA-idxB|
  // distribution across many MACs on a wide palette should hit at least
  // 3 distinct distances — not clamp to a single delta.
  std::vector<RGBW> raw{
      {255, 0, 0, 0}, {255, 128, 0, 0}, {255, 255, 0, 0},
      {0, 255, 0, 0}, {0, 255, 255, 0}, {0, 0, 255, 0},
      {128, 0, 255, 0}, {255, 0, 255, 0}};
  const uint32_t n = static_cast<uint32_t>(raw.size());
  std::vector<bool> seenDelta(n, false);
  for (uint32_t i = 0; i < 256; ++i) {
    uint8_t mac[6] = {static_cast<uint8_t>(i & 0xFF), 0x55, 0x66, 0x77, 0x88, 0x99};
    uint32_t idxA = hashMac(mac, 0u)      % n;
    uint32_t idxB = hashMac(mac, kGolden) % n;
    if (n >= 2 && idxA == idxB) idxB = (idxB + 1) % n;
    const uint32_t delta = idxA > idxB ? idxA - idxB : idxB - idxA;
    if (delta < n) seenDelta[delta] = true;
  }
  int distinctDeltas = 0;
  for (bool s : seenDelta) if (s) ++distinctDeltas;
  TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(
      3, distinctDeltas,
      "expected at least 3 distinct |idxA-idxB| values across a MAC sweep");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_palette_returns_black);
  RUN_TEST(test_single_color_palette_returns_color_for_both);
  RUN_TEST(test_two_color_palette_picks_pure_endpoints_no_blending);
  RUN_TEST(test_distinct_pair_when_palette_has_two_or_more_colors);
  RUN_TEST(test_same_mac_returns_same_tuple);
  RUN_TEST(test_swap_distribution_breaks_fixed_order);
  RUN_TEST(test_dedupe_drops_adjacent_near_identical_stops);
  RUN_TEST(test_picked_index_distance_varies_across_fleet);
  return UNITY_END();
}
