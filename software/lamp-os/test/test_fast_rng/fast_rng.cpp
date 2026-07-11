// Native-host unit tests for FastRng's seeded stream.
//
// The production class lives in src/util/fast_rng.hpp, which includes
// esp_system.h for esp_random() (the default ctor's seed) — unavailable on
// the native host. Following the convention in test/test_color/color.cpp, the
// seeded path under test is re-implemented inline here; the xorshift constants
// and the seeded-ctor forbidden-zero guard mirror the production contract.
//
// What matters: seeding from a per-lamp source (the efuse MAC) must give
// distinct lamps distinct streams — that's what stops the whole fleet rolling
// the same first-boot color. These tests pin determinism (same seed → same
// sequence) and divergence (different seeds → different hues).

#include <unity.h>

#include <cstdint>

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

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

// Same seed → identical sequence (so an un-persisted fresh lamp re-derives the
// same color every boot until the user configures one).
void test_same_seed_is_deterministic() {
  lamp::FastRng a(0x1234abcd);
  lamp::FastRng b(0x1234abcd);
  for (int i = 0; i < 16; i++) {
    TEST_ASSERT_EQUAL_UINT32(a.range(0, 359), b.range(0, 359));
  }
}

// Different seeds (different MACs) diverge on a hue draw within a few pulls —
// the guarantee that distinct lamps don't all boot the same color.
void test_distinct_seeds_diverge() {
  lamp::FastRng a(0x00000001);  // low-byte-different, like adjacent MACs
  lamp::FastRng b(0x00000002);
  bool diverged = false;
  for (int i = 0; i < 8; i++) {
    if (a.range(0, 359) != b.range(0, 359)) {
      diverged = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(diverged);
}

// Seed 0 is remapped to the fixed nonzero constant, so the stream never
// collapses to all-zeros (xorshift's absorbing state).
void test_zero_seed_does_not_stall() {
  lamp::FastRng z(0);
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) acc |= z.next();
  TEST_ASSERT_NOT_EQUAL(0u, acc);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_same_seed_is_deterministic);
  RUN_TEST(test_distinct_seeds_diverge);
  RUN_TEST(test_zero_seed_does_not_stall);
  return UNITY_END();
}
