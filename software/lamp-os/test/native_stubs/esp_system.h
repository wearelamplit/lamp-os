#pragma once
// Minimal fake for native tests that pull FastRng (src/util/fast_rng.hpp
// includes esp_system.h for esp_random()). Fixed value keeps seeded streams
// deterministic under test.
#include <cstdint>

inline uint32_t esp_random() { return 0xA3C59AC3u; }
