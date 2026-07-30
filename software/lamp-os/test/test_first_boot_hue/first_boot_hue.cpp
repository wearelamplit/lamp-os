// Native-host unit tests for the first-boot per-lamp hue derivation.
//
// A fresh, unadopted lamp rolls a default base+shade hue seeded from its efuse
// MAC (src/core/lamp.cpp: seedFromMac + the roll in Lamp::setup). Following the
// convention in test/test_color/color.cpp, the derivation under test is
// re-implemented inline; the native env doesn't link src/.
//
// The regression: FastRng's first draw reads only the high bits of one xorshift
// round, which don't avalanche low-byte seed differences. A raw-packed MAC seed
// therefore mapped every same-reel unit (shared OUI, near-sequential low byte)
// onto the identical hue. Hashing the MAC with FNV-1a first spreads every byte
// across the seed so the first draw diverges.

#include <unity.h>

#include <cstdint>
#include <set>

namespace lamp {

class FastRng {
 public:
  explicit FastRng(uint32_t s) : s_(s ? s : 0xA3C59AC3u) {}
  uint32_t next() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return s_;
  }
  uint32_t range(uint32_t lo, uint32_t hi) {
    if (hi <= lo) return lo;
    uint64_t span = static_cast<uint64_t>(hi - lo) + 1;
    return lo + static_cast<uint32_t>(
                    (static_cast<uint64_t>(next()) * span) >> 32);
  }
 private:
  uint32_t s_;
};

// Mirror of src/core/lamp.cpp::seedFromMac (the pure hashing half).
uint32_t seedFromMac(const uint8_t mac[6]) {
  uint32_t h = 0x811c9dc5u;
  for (int i = 0; i < 6; i++) {
    h ^= mac[i];
    h *= 0x01000193u;
  }
  return h;
}

// Mirror of the roll in Lamp::setup: base in [0,359], shade offset [60,300].
void huesFromMac(const uint8_t mac[6], int& baseHue, int& shadeHue) {
  FastRng rng(seedFromMac(mac));
  baseHue = static_cast<int>(rng.range(0, 359));
  shadeHue = (baseHue + static_cast<int>(rng.range(60, 300))) % 360;
}

// The raw-packed seed the fix replaced, kept only to prove the collapse.
uint32_t legacySeed(const uint8_t mac[6]) {
  return (uint32_t(mac[2]) << 24) | (uint32_t(mac[3]) << 16) |
         (uint32_t(mac[4]) << 8) | uint32_t(mac[5]);
}

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

static int circularDist(int a, int b) {
  int d = a > b ? a - b : b - a;
  return d < 360 - d ? d : 360 - d;
}

// The two field-observed MACs that collapsed onto the same red+purple now land
// on distinct base AND shade hues.
void test_example_macs_are_distinct() {
  const uint8_t a[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  const uint8_t b[6] = {0x14, 0x33, 0x5C, 0x09, 0x87, 0x8E};
  int ab, as, bb, bs;
  lamp::huesFromMac(a, ab, as);
  lamp::huesFromMac(b, bb, bs);
  TEST_ASSERT_TRUE(circularDist(ab, bb) >= 10);
  TEST_ASSERT_TRUE(circularDist(as, bs) >= 10);
}

// The actual failure mode: units off one manufacturing reel share the OUI and
// differ only in the low MAC byte. The legacy raw-packed seed mapped all of
// them to one hue; the hashed seed spreads them.
void test_sequential_reel_macs_do_not_collapse() {
  std::set<int> fixedHues, legacyHues;
  for (int i = 0; i < 8; i++) {
    uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, static_cast<uint8_t>(i)};
    int base, shade;
    lamp::huesFromMac(mac, base, shade);
    fixedHues.insert(base);

    lamp::FastRng legacy(lamp::legacySeed(mac));
    legacyHues.insert(static_cast<int>(legacy.range(0, 359)));
  }
  TEST_ASSERT_EQUAL_UINT32(1, legacyHues.size());   // documents the old bug
  TEST_ASSERT_TRUE(fixedHues.size() >= 6);          // fixed: near-all distinct
}

// Deterministic: an un-persisted fresh lamp re-derives the same hue each boot.
void test_same_mac_is_deterministic() {
  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34};
  int b1, s1, b2, s2;
  lamp::huesFromMac(mac, b1, s1);
  lamp::huesFromMac(mac, b2, s2);
  TEST_ASSERT_EQUAL_INT(b1, b2);
  TEST_ASSERT_EQUAL_INT(s1, s2);
}

// Sweep a spread of distinct MACs: base hues cover the wheel with no clustered
// collapse. 64 MACs across 360 degrees should hit many distinct hues and never
// pile too many onto one value.
void test_base_hues_are_well_spread() {
  std::set<int> hues;
  int maxSharing = 0;
  int counts[360] = {0};
  for (int i = 0; i < 64; i++) {
    uint8_t mac[6] = {0x02, static_cast<uint8_t>(0x11 * i),
                      static_cast<uint8_t>(0x37 + i * 7),
                      static_cast<uint8_t>(i * 13),
                      static_cast<uint8_t>(0xC0 ^ i),
                      static_cast<uint8_t>(i * 3 + 1)};
    int base, shade;
    lamp::huesFromMac(mac, base, shade);
    hues.insert(base);
    if (++counts[base] > maxSharing) maxSharing = counts[base];
  }
  TEST_ASSERT_TRUE(hues.size() >= 48);   // ~uniform over 64 draws
  TEST_ASSERT_TRUE(maxSharing <= 5);     // no collapse onto a single hue
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_example_macs_are_distinct);
  RUN_TEST(test_sequential_reel_macs_do_not_collapse);
  RUN_TEST(test_same_mac_is_deterministic);
  RUN_TEST(test_base_hues_are_well_spread);
  return UNITY_END();
}
