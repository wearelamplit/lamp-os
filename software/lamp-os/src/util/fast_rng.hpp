#pragma once

#include <cstdint>

#include "esp_system.h"

namespace lamp {

// Lightweight xorshift32 PRNG. ~8 bytes of state vs ~2.5 KB for std::mt19937
// per instance — the expressions only need uniform integer ranges for animation
// timing and color picks, never cryptographic quality. Seeded from esp_random()
// at construction; the xorshift forbidden-zero case maps to a fixed nonzero
// constant so the state is always usable.
class FastRng {
 public:
  FastRng() : s_(seed()) {}

  uint32_t next() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return s_;
  }

  // Inclusive [lo, hi]. Unbiased via 64-bit multiply; degrades to lo when
  // hi <= lo. Drop-in replacement for std::uniform_int_distribution<uint32_t>.
  uint32_t range(uint32_t lo, uint32_t hi) {
    if (hi <= lo) return lo;
    uint64_t span = static_cast<uint64_t>(hi - lo) + 1;
    return lo + static_cast<uint32_t>(
                    (static_cast<uint64_t>(next()) * span) >> 32);
  }

 private:
  static uint32_t seed() {
    uint32_t v = esp_random();
    return v ? v : 0xA3C59AC3u;
  }

  uint32_t s_;
};

}  // namespace lamp
