#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

// ponytail: reuse the one proven FNV-1a; don't hand-roll a second copy.
// FNV-1a 32-bit. Not cryptographic; spreads bytes across a small index set and
// stamps content hashes.
inline uint32_t fnv1a(const uint8_t* bytes, size_t n) {
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < n; ++i) {
    h ^= static_cast<uint32_t>(bytes[i]);
    h *= 0x01000193u;
  }
  return h;
}

}  // namespace wisp
