#include "relay_delay.hpp"

namespace lamp {

// One hash over all 15 bytes. A per-frame delay plus a per-lamp phase would fix
// each pair's offset forever, and a pair within one airtime would collide on
// every frame.
uint32_t relayDelayMs(const uint8_t ownMac[6], const uint8_t sourceMac[6],
                      uint8_t msgType, uint16_t seq, uint32_t minMs,
                      uint32_t maxMs) {
  uint32_t h = 0x811C9DC5u;
  auto fnv = [&h](uint8_t b) {
    h ^= b;
    h *= 0x01000193u;
  };
  for (int i = 0; i < 6; ++i) fnv(ownMac[i]);
  for (int i = 0; i < 6; ++i) fnv(sourceMac[i]);
  fnv(msgType);
  fnv(static_cast<uint8_t>(seq));
  fnv(static_cast<uint8_t>(seq >> 8));
  // FNV-1a leaves the last bytes' differences in the low bits; fmix32 spreads
  // them over the word before the modulo.
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return minMs + h % (maxMs - minMs + 1);
}

}  // namespace lamp
