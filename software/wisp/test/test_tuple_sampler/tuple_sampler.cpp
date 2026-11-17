// A local copy of the algorithm pins the contract independently (no
// CurrentPalette pull-in). The production sampleTupleAtPositions is
// also exercised directly, including a golden-parity check that guards the
// sampleTupleForMac refactor against the same golden bytes.

#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "paint/tuple_sampler.hpp"  // production sampleTupleAtPositions

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

uint8_t lerp8(uint8_t a, uint8_t b, uint32_t frac) {
  const uint64_t inv = 0x100000000ULL - static_cast<uint64_t>(frac);
  return static_cast<uint8_t>(
      ((static_cast<uint64_t>(a) * inv +
        static_cast<uint64_t>(b) * static_cast<uint64_t>(frac)) >> 32));
}

RGBW sampleGradientAt(const std::vector<RGBW>& stops, uint32_t pos) {
  const uint32_t n = static_cast<uint32_t>(stops.size());
  if (n == 1) return stops[0];
  const uint64_t scaled = static_cast<uint64_t>(pos) * (n - 1);
  const uint32_t i = static_cast<uint32_t>(scaled >> 32);
  const uint32_t frac = static_cast<uint32_t>(scaled & 0xFFFFFFFFu);
  if (i >= n - 1) return stops[n - 1];
  RGBW out;
  out.r = lerp8(stops[i].r, stops[i + 1].r, frac);
  out.g = lerp8(stops[i].g, stops[i + 1].g, frac);
  out.b = lerp8(stops[i].b, stops[i + 1].b, frac);
  out.w = lerp8(stops[i].w, stops[i + 1].w, frac);
  return out;
}

constexpr uint32_t kGolden   = 0x9E3779B9u;
constexpr uint32_t kSwapSalt = 0xCAFEBABEu;
constexpr uint32_t kMinGap   = 0x40000000u;

ColorTuple sampleTuple(const std::vector<RGBW>& raw, const uint8_t mac[6],
                       uint32_t shuffleSeed = 0) {
  ColorTuple out;
  if (raw.empty()) return out;
  auto stops = dedupe(raw);
  if (stops.empty()) return out;

  uint32_t posA = hashMac(mac, 0u      ^ shuffleSeed);
  uint32_t posB = hashMac(mac, kGolden ^ shuffleSeed);
  const uint32_t d = posA > posB ? posA - posB : posB - posA;
  if (d < kMinGap) {
    posB = (posA <= 0xFFFFFFFFu - kMinGap) ? posA + kMinGap : posA - kMinGap;
  }

  const bool swap = (hashMac(mac, kSwapSalt ^ shuffleSeed) & 1u) != 0u;
  const RGBW base  = sampleGradientAt(stops, swap ? posB : posA);
  const RGBW shade = sampleGradientAt(stops, swap ? posA : posB);
  out.r[0] = base.r;  out.g[0] = base.g;  out.b[0] = base.b;  out.w[0] = base.w;
  out.r[1] = shade.r; out.g[1] = shade.g; out.b[1] = shade.b; out.w[1] = shade.w;
  return out;
}

}  // namespace

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
  TEST_ASSERT_EQUAL_UINT8(50,  t.b[0]);
  TEST_ASSERT_EQUAL_UINT8(200, t.r[1]);
  TEST_ASSERT_EQUAL_UINT8(100, t.g[1]);
  TEST_ASSERT_EQUAL_UINT8(50,  t.b[1]);
}

void test_two_color_palette_produces_interpolated_colors(void) {
  // Gradient sampling: a 2-stop palette should produce in-between colors,
  // not just the two authored endpoints.
  const std::vector<RGBW> raw{{255, 128, 0, 0}, {0, 200, 80, 0}};  // orange, green
  bool foundBlend = false;
  for (uint32_t i = 0; i < 200 && !foundBlend; ++i) {
    uint8_t mac[6] = {static_cast<uint8_t>(i & 0xFF),
                      static_cast<uint8_t>((i >> 8) & 0xFF),
                      0x11, 0x22, 0x33, 0x44};
    ColorTuple t = sampleTuple(raw, mac);
    // Check base
    const bool baseIsOrange = (t.r[0]==255 && t.g[0]==128 && t.b[0]==0 && t.w[0]==0);
    const bool baseIsGreen  = (t.r[0]==0   && t.g[0]==200 && t.b[0]==80 && t.w[0]==0);
    if (!baseIsOrange && !baseIsGreen) foundBlend = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(foundBlend,
      "gradient sampling must produce interpolated (non-stop) colors");
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

void test_different_shuffle_seed_changes_result(void) {
  std::vector<RGBW> raw{{255, 128, 0, 0}, {0, 200, 80, 0}};
  bool foundDiff = false;
  for (uint32_t i = 0; i < 256 && !foundDiff; ++i) {
    uint8_t mac[6] = {static_cast<uint8_t>(i & 0xFF),
                      static_cast<uint8_t>((i >> 8) & 0xFF),
                      0xAA, 0xBB, 0xCC, 0xDD};
    ColorTuple a = sampleTuple(raw, mac, 0);
    ColorTuple b = sampleTuple(raw, mac, 1);
    if (a.r[0] != b.r[0] || a.g[0] != b.g[0] ||
        a.b[0] != b.b[0] || a.r[1] != b.r[1]) {
      foundDiff = true;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(foundDiff,
      "shuffleSeed=1 must yield a different result for at least one MAC");
}

void test_golden_parity(void) {
  // mac={0x10,0x20,0x30,0x40,0x50,0x60}, orange+green palette, seed=0.
  // Must match the Dart golden in tuple_sampler_test.dart byte-for-byte.
  const std::vector<RGBW> raw{{255, 128, 0, 0}, {0, 200, 80, 0}};
  const uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  ColorTuple t = sampleTuple(raw, mac, 0);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(178, t.r[0], "base.r");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(149, t.g[0], "base.g");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE( 23, t.b[0], "base.b");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(  0, t.w[0], "base.w");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(114, t.r[1], "shade.r");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(167, t.g[1], "shade.g");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE( 43, t.b[1], "shade.b");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(  0, t.w[1], "shade.w");
}

void test_dedupe_drops_adjacent_near_identical_stops(void) {
  // Two near-identical stops followed by a distinct one. Dedupe collapses
  // them; the effective palette size is exactly 2 distinct colors.
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

// Random drift path: sampleTupleAtPositions samples base/shade at two explicit
// positions. Exercised with fixed positions here; production feeds esp_random().
static std::vector<wisp::RGBW> orangeGreen() {
  return {{255, 128, 0, 0}, {0, 200, 80, 0}};
}

void test_at_positions_samples_base_at_posA(void) {
  // posA=0 on a 2-stop palette lands exactly on the first stop.
  wisp::ColorTuple t = wisp::sampleTupleAtPositions(orangeGreen(), 0u, 0xFFFFFFFFu);
  TEST_ASSERT_EQUAL_UINT8(255, t.r[0]);
  TEST_ASSERT_EQUAL_UINT8(128, t.g[0]);
  TEST_ASSERT_EQUAL_UINT8(0,   t.b[0]);
  TEST_ASSERT_EQUAL_UINT8(0,   t.w[0]);
  const bool baseDiffersFromShade =
      t.r[0] != t.r[1] || t.g[0] != t.g[1] || t.b[0] != t.b[1];
  TEST_ASSERT_TRUE(baseDiffersFromShade);
}

void test_at_positions_different_inputs_differ(void) {
  wisp::ColorTuple a = wisp::sampleTupleAtPositions(orangeGreen(), 0u, 0xFFFFFFFFu);
  wisp::ColorTuple b = wisp::sampleTupleAtPositions(orangeGreen(), 0x20000000u, 0xC0000000u);
  const bool differ = a.r[0] != b.r[0] || a.g[0] != b.g[0] || a.b[0] != b.b[0] ||
                      a.r[1] != b.r[1] || a.g[1] != b.g[1] || a.b[1] != b.b[1];
  TEST_ASSERT_TRUE_MESSAGE(differ,
      "distinct positions (a random drift slot) must yield a distinct pair");
}

void test_at_positions_gap_enforced_for_equal_inputs(void) {
  // Identical positions collapse to gap 0; the >= kMinGap separation must move
  // shade so base and shade stay visually distinct.
  wisp::ColorTuple t = wisp::sampleTupleAtPositions(orangeGreen(), 0x80000000u, 0x80000000u);
  const bool separated = t.r[0] != t.r[1] || t.g[0] != t.g[1] || t.b[0] != t.b[1];
  TEST_ASSERT_TRUE_MESSAGE(separated,
      "base/shade must stay >= kMinGap apart even for identical positions");
}

void test_golden_parity_production(void) {
  // Guard the sampleTupleForMac refactor: drive the golden mac's positions
  // through the PRODUCTION sampleTupleAtPositions (+ the swap sampleTupleForMac
  // applies) and assert the same golden bytes as the local-copy golden above.
  const uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  const uint32_t posA = hashMac(mac, 0u);
  const uint32_t posB = hashMac(mac, kGolden);
  wisp::ColorTuple t =
      wisp::sampleTupleAtPositions({{255, 128, 0, 0}, {0, 200, 80, 0}}, posA, posB);
  if ((hashMac(mac, kSwapSalt) & 1u) != 0u) {
    std::swap(t.r[0], t.r[1]); std::swap(t.g[0], t.g[1]);
    std::swap(t.b[0], t.b[1]); std::swap(t.w[0], t.w[1]);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(178, t.r[0], "base.r");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(149, t.g[0], "base.g");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE( 23, t.b[0], "base.b");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(  0, t.w[0], "base.w");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(114, t.r[1], "shade.r");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(167, t.g[1], "shade.g");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE( 43, t.b[1], "shade.b");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(  0, t.w[1], "shade.w");
}

void test_at_positions_empty_palette_returns_black(void) {
  std::vector<wisp::RGBW> empty;
  wisp::ColorTuple t = wisp::sampleTupleAtPositions(empty, 123u, 456u);
  for (int i = 0; i < 2; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0, t.r[i]);
    TEST_ASSERT_EQUAL_UINT8(0, t.g[i]);
    TEST_ASSERT_EQUAL_UINT8(0, t.b[i]);
    TEST_ASSERT_EQUAL_UINT8(0, t.w[i]);
  }
}

void test_tuple_rgbw_packs_w_per_surface(void) {
  wisp::ColorTuple t;
  t.r[0] = 1; t.g[0] = 2; t.b[0] = 3; t.w[0] = 0xC8;
  t.r[1] = 4; t.g[1] = 5; t.b[1] = 6; t.w[1] = 0x37;
  uint8_t base[4];
  uint8_t shade[4];
  t.rgbw(0, base);
  t.rgbw(1, shade);
  const uint8_t wantBase[4]  = {1, 2, 3, 0xC8};
  const uint8_t wantShade[4] = {4, 5, 6, 0x37};
  TEST_ASSERT_EQUAL_MEMORY(wantBase, base, 4);
  TEST_ASSERT_EQUAL_MEMORY(wantShade, shade, 4);
}

// Gradient through the production per-mac sampler: the same golden bytes as
// the local copy above.
void test_golden_parity_for_mac_production(void) {
  const uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  const wisp::ColorTuple t = wisp::sampleTupleForMac(orangeGreen(), mac, 0);
  const uint8_t wantBase[4]  = {178, 149, 23, 0};
  const uint8_t wantShade[4] = {114, 167, 43, 0};
  uint8_t base[4];
  uint8_t shade[4];
  t.rgbw(0, base);
  t.rgbw(1, shade);
  TEST_ASSERT_EQUAL_MEMORY(wantBase, base, 4);
  TEST_ASSERT_EQUAL_MEMORY(wantShade, shade, 4);
}

// Swatch vectors. The app's test/features/wisp/tuple_sampler_test.dart
// (predictTuple) mirrors them case for case; change both together.

using wisp::PaletteStyle;

static const std::vector<wisp::RGBW> kFour = {
    {255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}, {0, 0, 0, 255}};

static bool sameRgbw(const wisp::RGBW& c, const wisp::ColorTuple& t, int i) {
  return c.r == t.r[i] && c.g == t.g[i] && c.b == t.b[i] && c.w == t.w[i];
}

// Index of surface i's colour in `palette`, or -1 when it is no entry.
static int entryOf(const std::vector<wisp::RGBW>& palette,
                   const wisp::ColorTuple& t, int i) {
  for (size_t k = 0; k < palette.size(); ++k) {
    if (sameRgbw(palette[k], t, i)) return static_cast<int>(k);
  }
  return -1;
}

static bool sameBytes(const wisp::ColorTuple& t) {
  return t.r[0] == t.r[1] && t.g[0] == t.g[1] && t.b[0] == t.b[1] &&
         t.w[0] == t.w[1];
}

// Base is entry (posA * n) >> 32. Shade is the ((posB * m) >> 32)-th of the m
// entries whose bytes differ from the base, in palette order.
void test_swatch_positions_pick_entries(void) {
  struct Case { uint32_t posA, posB; int base, shade; };
  const Case cases[] = {
      {0u, 0u, 0, 1},
      {0xFFFFFFFFu, 0xFFFFFFFFu, 3, 2},
      {0x80000000u, 0x80000000u, 2, 1},
      {0x40000000u, 0xC0000000u, 1, 3},
  };
  for (const Case& c : cases) {
    const wisp::ColorTuple t = wisp::sampleTupleAtPositions(
        kFour, c.posA, c.posB, PaletteStyle::Swatches);
    TEST_ASSERT_EQUAL_INT(c.base, entryOf(kFour, t, 0));
    TEST_ASSERT_EQUAL_INT(c.shade, entryOf(kFour, t, 1));
  }
  // A repeat of the base is not a shade candidate: of [A, B, A, C] with base
  // A the candidates are B and C, and the middle position lands on C.
  const wisp::RGBW a{10, 20, 30, 40};
  const wisp::RGBW b{50, 60, 70, 80};
  const wisp::RGBW c{90, 100, 110, 120};
  const std::vector<wisp::RGBW> abac = {a, b, a, c};
  const wisp::ColorTuple t = wisp::sampleTupleAtPositions(
      abac, 0u, 0x80000000u, PaletteStyle::Swatches);
  TEST_ASSERT_TRUE(sameRgbw(a, t, 0));
  TEST_ASSERT_TRUE(sameRgbw(c, t, 1));
}

// Six lamps, shuffle seeds 0 and 7: the exact entries each lamp gets.
void test_swatch_golden_per_mac(void) {
  const int want[2][6][2] = {
      {{2, 3}, {0, 1}, {3, 1}, {0, 2}, {1, 0}, {1, 2}},  // seed 0
      {{0, 1}, {1, 3}, {3, 0}, {1, 0}, {0, 1}, {2, 3}},  // seed 7
  };
  const uint32_t seeds[2] = {0, 7};
  for (int s = 0; s < 2; ++s) {
    for (uint8_t i = 0; i < 6; ++i) {
      const uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, i};
      const wisp::ColorTuple t = wisp::sampleTupleForMac(
          kFour, mac, seeds[s], PaletteStyle::Swatches);
      TEST_ASSERT_EQUAL_INT(want[s][i][0], entryOf(kFour, t, 0));
      TEST_ASSERT_EQUAL_INT(want[s][i][1], entryOf(kFour, t, 1));
    }
  }
}

// A colour repeated k times in n is the base on about k / n of the lamps, and
// with two distinct colours every lamp shows both.
void test_swatch_base_weight_follows_repetition(void) {
  const wisp::RGBW a{200, 40, 10, 0};
  const wisp::RGBW b{10, 40, 200, 0};
  const std::vector<wisp::RGBW> aaab = {a, a, a, b};
  int baseIsA = 0;
  for (int i = 0; i < 64; ++i) {
    const uint8_t mac[6] = {0x24, 0x6f, 0x28, 0x00, static_cast<uint8_t>(i >> 4),
                            static_cast<uint8_t>(i & 15)};
    const wisp::ColorTuple t =
        wisp::sampleTupleForMac(aaab, mac, 0, PaletteStyle::Swatches);
    TEST_ASSERT_TRUE(sameRgbw(a, t, 0) || sameRgbw(b, t, 0));
    TEST_ASSERT_FALSE(sameBytes(t));
    if (sameRgbw(a, t, 0)) ++baseIsA;
  }
  // 48 expected; a palette weighed as [A, B] lands near 32.
  TEST_ASSERT_INT_WITHIN(8, 48, baseIsA);
}

void test_swatch_single_colour_fills_both(void) {
  const std::vector<wisp::RGBW> one = {{12, 34, 56, 78}};
  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
  const wisp::ColorTuple t =
      wisp::sampleTupleForMac(one, mac, 3, PaletteStyle::Swatches);
  TEST_ASSERT_TRUE(sameRgbw(one[0], t, 0));
  TEST_ASSERT_TRUE(sameRgbw(one[0], t, 1));
}

void test_swatch_shuffle_changes_picks(void) {
  bool changed = false;
  for (uint8_t i = 0; i < 6 && !changed; ++i) {
    const uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, i};
    const wisp::ColorTuple t0 =
        wisp::sampleTupleForMac(kFour, mac, 0, PaletteStyle::Swatches);
    const wisp::ColorTuple t7 =
        wisp::sampleTupleForMac(kFour, mac, 7, PaletteStyle::Swatches);
    changed = entryOf(kFour, t0, 0) != entryOf(kFour, t7, 0) ||
              entryOf(kFour, t0, 1) != entryOf(kFour, t7, 1);
  }
  TEST_ASSERT_TRUE(changed);
}

// Swatches take the palette as given: near-duplicates that gradient merges
// are separate swatches, and each is the other's shade.
void test_swatch_keeps_near_duplicates(void) {
  const std::vector<wisp::RGBW> near = {{100, 0, 0, 0}, {104, 0, 0, 0}};
  const wisp::ColorTuple lo = wisp::sampleTupleAtPositions(
      near, 0u, 0u, PaletteStyle::Swatches);
  const wisp::ColorTuple hi = wisp::sampleTupleAtPositions(
      near, 0xFFFFFFFFu, 0u, PaletteStyle::Swatches);
  TEST_ASSERT_EQUAL_INT(0, entryOf(near, lo, 0));
  TEST_ASSERT_EQUAL_INT(1, entryOf(near, lo, 1));
  TEST_ASSERT_EQUAL_INT(1, entryOf(near, hi, 0));
  TEST_ASSERT_EQUAL_INT(0, entryOf(near, hi, 1));
}

// Drift draws random positions; in swatches every draw is two different
// palette entries exactly.
void test_drift_draws_use_the_style(void) {
  uint32_t x = 0x12345678u;
  auto next = [&x] { x = x * 1664525u + 1013904223u; return x; };
  for (int i = 0; i < 200; ++i) {
    const uint32_t posA = next();
    const uint32_t posB = next();
    const wisp::ColorTuple t = wisp::sampleTupleAtPositions(
        kFour, posA, posB, PaletteStyle::Swatches);
    TEST_ASSERT_NOT_EQUAL(-1, entryOf(kFour, t, 0));
    TEST_ASSERT_NOT_EQUAL(-1, entryOf(kFour, t, 1));
    TEST_ASSERT_FALSE(sameBytes(t));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_palette_returns_black);
  RUN_TEST(test_single_color_palette_returns_color_for_both);
  RUN_TEST(test_two_color_palette_produces_interpolated_colors);
  RUN_TEST(test_same_mac_returns_same_tuple);
  RUN_TEST(test_different_shuffle_seed_changes_result);
  RUN_TEST(test_golden_parity);
  RUN_TEST(test_dedupe_drops_adjacent_near_identical_stops);
  RUN_TEST(test_golden_parity_production);
  RUN_TEST(test_at_positions_samples_base_at_posA);
  RUN_TEST(test_at_positions_different_inputs_differ);
  RUN_TEST(test_at_positions_gap_enforced_for_equal_inputs);
  RUN_TEST(test_at_positions_empty_palette_returns_black);
  RUN_TEST(test_tuple_rgbw_packs_w_per_surface);
  RUN_TEST(test_golden_parity_for_mac_production);
  RUN_TEST(test_swatch_positions_pick_entries);
  RUN_TEST(test_swatch_golden_per_mac);
  RUN_TEST(test_swatch_base_weight_follows_repetition);
  RUN_TEST(test_swatch_single_colour_fills_both);
  RUN_TEST(test_swatch_shuffle_changes_picks);
  RUN_TEST(test_swatch_keeps_near_duplicates);
  RUN_TEST(test_drift_draws_use_the_style);
  return UNITY_END();
}
