#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <lampos/protocol/dedup_ring.hpp>
#include <lampos/protocol/header.hpp>

namespace lamp {

// True when targetMac is this lamp or the all-FF broadcast address.
inline bool addressedToUs(const uint8_t targetMac[6], const uint8_t myMac[6]) {
  static constexpr uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return std::memcmp(targetMac, myMac, 6) == 0 ||
         std::memcmp(targetMac, kBroadcast, 6) == 0;
}

// Admits a targeted single-hop frame that is new to this lamp. A frame for
// another lamp never takes a slot in this lamp's ring, so neighbouring
// sessions' traffic cannot evict the tuples that dedup this lamp's own.
template <size_t N>
bool admitAddressed(lamp_protocol::DedupRing<N>& ring,
                    const uint8_t sourceMac[6], uint8_t msgType, uint16_t seq,
                    const uint8_t targetMac[6], const uint8_t myMac[6],
                    uint32_t nowMs) {
  return addressedToUs(targetMac, myMac) &&
         ring.record(sourceMac, msgType, seq, nowMs);
}

enum class HelloAdmit : uint8_t { Own, Repeat, Fresh };

// Classifies an inbound HELLO. A lamp's own HELLO echoed back by a relayer is
// Own and never takes a ring slot.
template <size_t N>
HelloAdmit admitHello(lamp_protocol::DedupRing<N>& ring,
                      const uint8_t sourceMac[6], uint16_t seq,
                      const uint8_t myMac[6], uint32_t nowMs) {
  if (std::memcmp(sourceMac, myMac, 6) == 0) return HelloAdmit::Own;
  return ring.record(sourceMac, lamp_protocol::MSG_HELLO, seq, nowMs)
             ? HelloAdmit::Fresh
             : HelloAdmit::Repeat;
}

}  // namespace lamp
